#include "net/tls.hh"
#include "hash/sha256.hh"
#include "base/system/clock.hh"
#include "base/memory.hh"
#include "base/strings.hh"
#include <winsock2.h>
#include <wincrypt.h>
#include <security.h>
#include <subauth.h>
#include <schannel.h>

namespace net {

	static CredHandle* credential_of ( uint8_t* bytes ) {

		return reinterpret_cast < CredHandle* > ( bytes );

	}

	static CtxtHandle* context_of ( uint8_t* bytes ) {

		return reinterpret_cast < CtxtHandle* > ( bytes );

	}

	static bool send_all ( uintptr_t socket, const char* data, int32_t size ) {

		while ( size > 0 ) {

			int sent = ::send ( static_cast < SOCKET > ( socket ), data, size, 0 );

			if ( sent <= 0 )
				return false;

			data += sent;
			size -= sent;

		}

		return true;

	}

	static bool readable ( uintptr_t socket, double deadline ) {

		double left = deadline - base::seconds ( );

		if ( left <= 0.0 )
			return false;

		fd_set set;
		uint32_t wait = static_cast < uint32_t > ( left * 1000.0 );
		timeval limit = { static_cast < long > ( wait / 1000 ), static_cast < long > ( wait % 1000 * 1000 ) };

		set.fd_count = 1;
		set.fd_array [ 0 ] = static_cast < SOCKET > ( socket );

		return select ( 0, &set, nullptr, nullptr, &limit ) > 0;

	}

	tls_session::~tls_session ( ) {

		if ( has_context )
			DeleteSecurityContext ( context_of ( context ) );

		if ( has_credentials )
			FreeCredentialsHandle ( credential_of ( credentials ) );

	}

	bool tls_session::handshake ( uintptr_t socket, const char* host, uint32_t timeout ) {

		SCH_CREDENTIALS settings = { };
		TimeStamp expiry;

		base::widen ( host_name, base::count_of ( host_name ), host );
		settings.dwVersion = SCH_CREDENTIALS_VERSION;
		settings.dwFlags = SCH_USE_STRONG_CRYPTO | SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION;
		last_status = AcquireCredentialsHandleW ( nullptr, const_cast < wchar_t* > ( UNISP_NAME_W ), SECPKG_CRED_OUTBOUND, nullptr, &settings, nullptr, nullptr, credential_of ( credentials ), &expiry );

		if ( last_status != SEC_E_OK )
			return false;

		has_credentials = true;

		if ( !exchange ( socket, host, timeout ) )
			return false;

		SecPkgContext_StreamSizes sizes = { };
		SecPkgContext_ConnectionInfo info = { };

		last_status = QueryContextAttributesW ( context_of ( context ), SECPKG_ATTR_STREAM_SIZES, &sizes );

		if ( last_status != SEC_E_OK )
			return false;

		if ( QueryContextAttributesW ( context_of ( context ), SECPKG_ATTR_CONNECTION_INFO, &info ) == SEC_E_OK )
			version = info.dwProtocol;

		header = sizes.cbHeader;
		trailer = sizes.cbTrailer;
		largest = base::min ( sizes.cbMaximumMessage, static_cast < unsigned long > ( tls_room - 1024 ) );
		ready = true;
		inspect ( );

		return true;

	}

	void tls_session::inspect ( ) {

		PCCERT_CONTEXT certificate = nullptr;

		chain_ok = false;
		chain_error = static_cast < unsigned long > ( CERT_E_UNTRUSTEDROOT );

		if ( QueryContextAttributesW ( context_of ( context ), SECPKG_ATTR_REMOTE_CERT_CONTEXT, &certificate ) != SEC_E_OK || !certificate )
			return;

		hash::sha256_once ( certificate->pbCertEncoded, certificate->cbCertEncoded, print );

		static const char* usages [ ] = { szOID_PKIX_KP_SERVER_AUTH, szOID_SERVER_GATED_CRYPTO, szOID_SGC_NETSCAPE };
		CERT_CHAIN_PARA chain_settings = { };
		PCCERT_CHAIN_CONTEXT chain = nullptr;

		chain_settings.cbSize = sizeof ( chain_settings );
		chain_settings.RequestedUsage.dwType = USAGE_MATCH_TYPE_OR;
		chain_settings.RequestedUsage.Usage.cUsageIdentifier = 3;
		chain_settings.RequestedUsage.Usage.rgpszUsageIdentifier = const_cast < char** > ( usages );

		if ( CertGetCertificateChain ( nullptr, certificate, nullptr, certificate->hCertStore, &chain_settings, 0, nullptr, &chain ) ) {

			HTTPSPolicyCallbackData extra = { };
			CERT_CHAIN_POLICY_PARA policy = { };
			CERT_CHAIN_POLICY_STATUS verdict = { };

			extra.cbStruct = sizeof ( extra );
			extra.dwAuthType = AUTHTYPE_SERVER;
			extra.pwszServerName = host_name;
			policy.cbSize = sizeof ( policy );
			policy.pvExtraPolicyPara = &extra;
			verdict.cbSize = sizeof ( verdict );

			if ( CertVerifyCertificateChainPolicy ( CERT_CHAIN_POLICY_SSL, chain, &policy, &verdict ) ) {

				chain_error = verdict.dwError;
				chain_ok = verdict.dwError == 0;

			}

			CertFreeCertificateChain ( chain );

		}

		CertFreeCertificateContext ( certificate );

	}

	const wchar_t* tls_session::trust_problem ( ) const {

		switch ( chain_error ) {

			case static_cast < unsigned long > ( CERT_E_EXPIRED ):
				return L"its certificate has expired";

			case static_cast < unsigned long > ( CERT_E_CN_NO_MATCH ):
				return L"its certificate is for a different name";

			case static_cast < unsigned long > ( CERT_E_UNTRUSTEDROOT ):
			case static_cast < unsigned long > ( CERT_E_CHAINING ):
				return L"its certificate isn't signed by an authority Windows trusts";

			default:
				return L"Windows doesn't trust its certificate";

		}

	}

	bool tls_session::exchange ( uintptr_t socket, const char* host, uint32_t timeout ) {

		DWORD wanted = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM | ISC_REQ_EXTENDED_ERROR | ISC_REQ_MANUAL_CRED_VALIDATION;
		double deadline = base::seconds ( ) + static_cast < double > ( timeout ) / 1000.0;
		bool need_input = has_context && incoming_size == 0 && !ready;

		for ( ;; ) {

			if ( need_input ) {

				if ( incoming_size >= tls_room || !readable ( socket, deadline ) )
					return false;

				int received = recv ( static_cast < SOCKET > ( socket ), incoming + incoming_size, tls_room - incoming_size, 0 );

				if ( received <= 0 )
					return false;

				incoming_size += received;

			}

			SecBuffer inputs [ 2 ] = { { static_cast < unsigned long > ( incoming_size ), SECBUFFER_TOKEN, incoming }, { 0, SECBUFFER_EMPTY, nullptr } };
			SecBuffer outputs [ 1 ] = { { 0, SECBUFFER_TOKEN, nullptr } };
			SecBufferDesc input = { SECBUFFER_VERSION, 2, inputs };
			SecBufferDesc output = { SECBUFFER_VERSION, 1, outputs };
			DWORD granted = 0;
			bool first = !has_context;

			last_status = InitializeSecurityContextW ( credential_of ( credentials ), first ? nullptr : context_of ( context ), host_name, wanted, 0, 0, first ? nullptr : &input, 0, context_of ( context ), &output, &granted, nullptr );

			if ( first && ( last_status == SEC_E_OK || last_status == SEC_I_CONTINUE_NEEDED ) )
				has_context = true;
			if ( outputs [ 0 ].pvBuffer ) {

				bool sent = outputs [ 0 ].cbBuffer == 0 || send_all ( socket, static_cast < const char* > ( outputs [ 0 ].pvBuffer ), static_cast < int32_t > ( outputs [ 0 ].cbBuffer ) );

				FreeContextBuffer ( outputs [ 0 ].pvBuffer );

				if ( !sent )
					return false;

			}

			if ( last_status == SEC_E_INCOMPLETE_MESSAGE ) {

				need_input = true;

				continue;

			}

			if ( last_status != SEC_E_OK && last_status != SEC_I_CONTINUE_NEEDED )
				return false;

			if ( !first && inputs [ 1 ].BufferType == SECBUFFER_EXTRA ) {

				int32_t extra = static_cast < int32_t > ( inputs [ 1 ].cbBuffer );

				base::move ( incoming, incoming + incoming_size - extra, static_cast < size_t > ( extra ) );
				incoming_size = extra;

			} else if ( !first )
				incoming_size = 0;

			if ( last_status == SEC_E_OK )
				return true;

			need_input = incoming_size == 0;

		}

	}

	bool tls_session::send ( uintptr_t socket, const char* data, int32_t size ) {

		char* buffer = new char [ tls_room ];
		bool ok = ready;

		while ( ok && size > 0 ) {

			int32_t chunk = base::min ( size, static_cast < int32_t > ( largest ) );
			SecBuffer parts [ 4 ] = {

				{ header, SECBUFFER_STREAM_HEADER, buffer },
				{ static_cast < unsigned long > ( chunk ), SECBUFFER_DATA, buffer + header },
				{ trailer, SECBUFFER_STREAM_TRAILER, buffer + header + chunk },
				{ 0, SECBUFFER_EMPTY, nullptr },

			};
			SecBufferDesc message = { SECBUFFER_VERSION, 4, parts };

			base::copy ( buffer + header, data, static_cast < size_t > ( chunk ) );
			last_status = EncryptMessage ( context_of ( context ), 0, &message, 0 );
			ok = last_status == SEC_E_OK && send_all ( socket, buffer, static_cast < int32_t > ( parts [ 0 ].cbBuffer + parts [ 1 ].cbBuffer + parts [ 2 ].cbBuffer ) );
			data += chunk;
			size -= chunk;

		}

		delete [ ] buffer;

		return ok;

	}

	int32_t tls_session::receive ( uintptr_t socket, char* plain, int32_t room, bool socket_ready ) {

		if ( !ready )
			return -1;

		if ( incoming_size < tls_room ) {

			u_long waiting = 0;

			ioctlsocket ( static_cast < SOCKET > ( socket ), FIONREAD, &waiting );

			if ( waiting || socket_ready ) {

				int received = recv ( static_cast < SOCKET > ( socket ), incoming + incoming_size, tls_room - incoming_size, 0 );

				if ( received <= 0 )
					return -1;

				incoming_size += received;

			}

		}

		int32_t produced = 0;

		for ( int32_t round = 0; round < 16; round++ ) {

			int32_t got = drain ( plain + produced, room - produced );

			if ( got < 0 )
				return produced ? produced : -1;

			produced += got;

			if ( last_status != SEC_I_RENEGOTIATE )
				break;

			char name [ 256 ];

			base::narrow ( name, sizeof ( name ), host_name );

			if ( !exchange ( socket, name, 5000 ) )
				return produced ? produced : -1;

		}

		return produced;

	}

	int32_t tls_session::drain ( char* plain, int32_t room ) {

		int32_t produced = 0;

		while ( incoming_size > 0 ) {

			SecBuffer parts [ 4 ] = { { static_cast < unsigned long > ( incoming_size ), SECBUFFER_DATA, incoming }, { 0, SECBUFFER_EMPTY, nullptr }, { 0, SECBUFFER_EMPTY, nullptr }, { 0, SECBUFFER_EMPTY, nullptr } };
			SecBufferDesc message = { SECBUFFER_VERSION, 4, parts };

			last_status = DecryptMessage ( context_of ( context ), &message, 0, nullptr );
			if ( last_status == SEC_E_INCOMPLETE_MESSAGE ) {

				last_status = SEC_E_OK;

				break;

			}

			if ( last_status != SEC_E_OK && last_status != SEC_I_RENEGOTIATE )
				return produced ? produced : -1;

			SecBuffer* content = nullptr;
			SecBuffer* extra = nullptr;

			for ( SecBuffer& part : parts ) {

				if ( part.BufferType == SECBUFFER_DATA )
					content = &part;
				else if ( part.BufferType == SECBUFFER_EXTRA )
					extra = &part;

			}

			if ( content && content->cbBuffer ) {

				int32_t size = static_cast < int32_t > ( content->cbBuffer );

				if ( produced + size > room )
					return -1;

				base::copy ( plain + produced, content->pvBuffer, static_cast < size_t > ( size ) );
				produced += size;

			}

			if ( extra ) {

				int32_t left = static_cast < int32_t > ( extra->cbBuffer );
				const char* from = extra->pvBuffer ? static_cast < const char* > ( extra->pvBuffer ) : incoming + incoming_size - left;

				base::move ( incoming, from, static_cast < size_t > ( left ) );
				incoming_size = left;

			} else
				incoming_size = 0;

			if ( last_status == SEC_I_RENEGOTIATE )
				break;

		}

		return produced;

	}

	void tls_session::close ( uintptr_t socket ) {

		if ( !ready )
			return;

		ready = false;

		DWORD kind = SCHANNEL_SHUTDOWN;
		SecBuffer token = { sizeof ( kind ), SECBUFFER_TOKEN, &kind };
		SecBufferDesc control = { SECBUFFER_VERSION, 1, &token };

		if ( ApplyControlToken ( context_of ( context ), &control ) != SEC_E_OK )
			return;

		SecBuffer outputs [ 1 ] = { { 0, SECBUFFER_TOKEN, nullptr } };
		SecBufferDesc output = { SECBUFFER_VERSION, 1, outputs };
		DWORD granted = 0;
		DWORD wanted = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;

		if ( InitializeSecurityContextW ( credential_of ( credentials ), context_of ( context ), host_name, wanted, 0, 0, nullptr, 0, context_of ( context ), &output, &granted, nullptr ) == SEC_E_OK && outputs [ 0 ].pvBuffer ) {

			send_all ( socket, static_cast < const char* > ( outputs [ 0 ].pvBuffer ), static_cast < int32_t > ( outputs [ 0 ].cbBuffer ) );
			FreeContextBuffer ( outputs [ 0 ].pvBuffer );

		}

	}

	const wchar_t* tls_session::problem ( ) const {

		switch ( static_cast < unsigned long > ( last_status ) ) {

			case static_cast < unsigned long > ( SEC_E_UNTRUSTED_ROOT ):
				return L"the pool's certificate isn't signed by an authority Windows trusts";

			case static_cast < unsigned long > ( SEC_E_WRONG_PRINCIPAL ):
				return L"the pool's certificate is for a different name";

			case static_cast < unsigned long > ( SEC_E_CERT_EXPIRED ):
				return L"the pool's certificate has expired";

			case static_cast < unsigned long > ( SEC_E_ALGORITHM_MISMATCH ):
				return L"the pool and Windows share no cipher";

			default:
				return L"the secure handshake failed";

		}

	}

	const wchar_t* tls_session::protocol ( ) const {

		if ( version & ( SP_PROT_TLS1_3_CLIENT | SP_PROT_TLS1_3_SERVER ) )
			return L"TLS 1.3";

		if ( version & ( SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_2_SERVER ) )
			return L"TLS 1.2";

		return L"TLS";

	}

}

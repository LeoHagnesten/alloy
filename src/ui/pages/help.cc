#include "ui/shell.hh"
#include "version.hh"
#include "base/math.hh"
#include "base/text.hh"

namespace ui {

	struct help_section {

		const wchar_t* title;
		const wchar_t* body;

	};

	static const help_section topics [ ] = {

		{ L"GETTING STARTED", L"Pick an algorithm on the left and the coin at the top of Settings, put a pool address and your wallet there too, then press Start. SHA-256d covers Bitcoin, Bitcoin Cash, eCash, Bitcoin SV and DigiByte; scrypt covers Litecoin, Dogecoin and DigiByte; RandomX covers Monero, Wownero, Zephyr and six more; kHeavyHash mines Kaspa; Ethash mines Ethereum Classic and EthereumPoW. Pool mode mines for a pool that pays you out over time. Benchmark measures how fast this computer hashes without sending anything anywhere. Genesis rediscovers the nonce of Bitcoin's or Litecoin's first block, as a demonstration that takes minutes to hours. Kaspa mines with kHeavyHash on the graphics card through any pool that runs the Kaspa Stratum bridge. Ethash runs only on the graphics card: before the first share the card builds a DAG of several gigabytes for the current epoch, about 4.3 GB for Ethereum Classic, which takes a few seconds and shows on Overview. A card with too little memory sits that epoch out. Rest the cursor on the Overview graph and a gear appears in its corner for the graph's type, span, update rate, smoothing and time labels." },
		{ L"SHARES AND DIFFICULTY", L"A pool hands out work and a share difficulty. Every hash that beats that difficulty is a share, proof that this computer is working. The pool pays you in proportion to your shares when its own miners find blocks. A block needs a hash that beats the network difficulty, which is far higher, so a single computer almost never finds one alone. Rejected shares usually arrived after the pool moved on to a new job, or came from an address the pool doesn't accept." },
		{ L"STRATUM V2", L"For a Stratum V2 pool, put stratum2+tcp://host:port/ followed by the pool's authority key in Pool address. Alloy runs the Noise handshake, so everything on the wire is encrypted, and it only mines once the pool's certificate checks out against that key. Alloy opens a standard channel, so the pool picks the transactions and Alloy rolls the time and nonce." },
		{ L"SOLO MINING", L"Put your own node's RPC address in Pool address, such as http://127.0.0.1:8332 for Bitcoin Core or http://127.0.0.1:18081 for monerod, and the address that gets the block reward in Worker. Alloy asks the node for block templates, builds the coinbase itself and hands every block it finds back to the node. Bitcoin Core's cookie file is read automatically; otherwise put user:password in Password. Earnings shows the odds of finding a block, which for one computer are usually very long." },
		{ L"EARNINGS", L"Earnings counts every accepted share as its slice of the block reward at the network difficulty when it was found, before the pool's fee, and keeps a record of every day. Alloy reads the pending balance from SupportXMR and CKPool. The value in money appears when you turn it on, from CoinGecko's public prices. Mining Bitcoin or Litecoin on a computer earns next to nothing next to dedicated machines, so treat those numbers as the curiosity they are." },
		{ L"WHAT EACH SETTING COSTS", L"More threads and a higher usage limit mean more hashes, more heat and more power. Priority decides who wins when other programs want the processor: Idle gets out of their way entirely. The graphics card's intensity is how much work goes out in one batch; higher is a little faster but can make video stutter. The temperature limit rests the card at a set temperature. RandomX's Memory slider runs from light mode's 256 MB to the full 2.3 GB, which hashes about twelve times faster; slices in between gain most near the top. Pausing while you use the computer, on battery, for full-screen games or on a schedule all stop mining completely until the reason goes away." },
		{ L"ADVANCED SETTINGS", L"Turn on Advanced settings at the top of Settings for the controls experienced miners reach for. Start difficulty asks the pool for a share difficulty right after signing in. Tries before the backup pool, Back to the main pool after, Connection timeout, Reconnect after silence and Longest wait between tries shape how Alloy handles a pool that drops. Cores pins the threads to exactly the logical processors you pick. Scrypt and RandomX get a kernel choice for comparing speeds, solo blocks carry the coinbase message you type, Hashrate average sets how long the big number averages over, and a pool fee, power draw and power price turn Earnings into earnings after the fee, power cost and profit per day. Every one of them is also a line in settings.ini and works with --set." },
		{ L"RIGS AND THE WEB DASHBOARD", L"Turn on the stats API to read Alloy from a browser at http://127.0.0.1:4067, or from scripts at /api.json. Let other computers connect to watch this rig from phones and other rigs; they need the access token, sent as Authorization: Bearer or put after the address as #token=. With pause and resume allowed, POST /1/pause, /1/resume, /1/start and /1/stop control mining." },
		{ L"WHEN SOMETHING GOES WRONG", L"Every event lands in Activity, and in a log file kept for two weeks. If Alloy ever crashes it writes a report with a stack trace next to the logs and says so the next time it opens. Alloy tests every hash kernel and graphics card against known answers when it starts, and won't mine with one that gives a wrong answer. Run Alloy.exe --self-test to see the checks yourself." },

	};

	static const wchar_t* const shortcuts [ ] [ 2 ] = {

		{ L"F5", L"Start or stop mining" },
		{ L"Ctrl+Alt+M", L"Pause or resume from anywhere, even with the window closed" },
		{ L"Ctrl+1 to Ctrl+6", L"Open a page" },
		{ L"Tab, Shift+Tab", L"Move between controls" },
		{ L"Enter, Space", L"Press the focused control" },
		{ L"Left, Right", L"Change the focused choice, slider or stepper" },
		{ L"Escape", L"Leave a text field or hide the focus ring" },
		{ L"Ctrl+Z, Ctrl+Y", L"Undo and redo a setting or the text in a field" },
		{ L"Ctrl+C, Ctrl+V", L"Copy and paste in text fields" },

	};

	static const wchar_t* const commands [ ] [ 2 ] = {

		{ L"--bench [algorithm]", L"Measure the hashrate" },
		{ L"--mine host:port --user wallet", L"Mine from the command line" },
		{ L"--genesis", L"Rediscover the genesis block's nonce" },
		{ L"--test-pool", L"Run a local pool for trying things out" },
		{ L"--self-test", L"Check every kernel against known answers" },
		{ L"--profile name", L"Open with a saved profile" },
		{ L"--set key=value", L"Change any setting for this run" },
		{ L"--tune", L"Find the fastest threads and intensity" },

	};

	void shell::paint_help ( render::surface& surface, const snapshot& view, render::box area, double now ) {

		float offset = scroll_offset ( SCROLL_HELP, now );
		float x = area.x + 28.0f;
		float wide = base::min ( area.w - 56.0f, 760.0f );
		float y = area.y + 6.0f;

		scroll_area [ SCROLL_HELP ] = area;
		surface.push_shift ( 0.0f, -offset );
		hit_offset = -offset;
		hit_top = area.y;
		hit_bottom = area.bottom ( );
		hit_scroller = SCROLL_HELP;

		for ( const help_section& topic : topics ) {

			y += 18.0f;
			caption ( surface, render::box { x, y, wide, 16.0f }, topic.title );
			y += 24.0f;

			float tall = surface.paragraph_height ( topic.body, render::FONT_BODY, wide );

			surface.paragraph ( topic.body, render::FONT_BODY, render::box { x, y, wide, tall + 4.0f }, colors.muted );
			y += tall + 8.0f;

		}

		auto table = [ & ] ( const wchar_t* title, const wchar_t* const ( *rows ) [ 2 ], int32_t count, render::font left ) {

			y += 18.0f;
			caption ( surface, render::box { x, y, wide, 16.0f }, title );
			y += 22.0f;
			surface.rule ( x, x + wide, y - 1.0f, colors.divider );

			for ( int32_t i = 0; i < count; i++ ) {

				surface.text ( rows [ i ] [ 0 ], left, render::box { x, y, wide * 0.4f, 30.0f }, colors.text );
				surface.text ( rows [ i ] [ 1 ], render::FONT_BODY, render::box { x + wide * 0.4f, y, wide * 0.6f, 30.0f }, colors.muted );
				y += 30.0f;
				surface.rule ( x, x + wide, y - 1.0f, colors.divider );

			}

		};

		table ( L"KEYBOARD", shortcuts, base::count_of ( shortcuts ), render::FONT_STRONG );
		table ( L"COMMAND LINE", commands, base::count_of ( commands ), render::FONT_MONO );

		y += 18.0f;
		caption ( surface, render::box { x, y, wide, 16.0f }, L"ABOUT" );
		y += 24.0f;
		surface.text ( L"Alloy " ALLOY_VERSION_TEXT, render::FONT_STRONG, render::box { x, y, wide, 24.0f }, colors.text );
		y += 24.0f;
		surface.text ( L"Made by Leo Hagnesten.", render::FONT_BODY, render::box { x, y, wide, 24.0f }, colors.muted );
		y += 24.0f;

		const wchar_t* credits = L"Every hash, the RandomX virtual machine and its compiler, the pool protocols, the graphics shaders and this window are written from scratch in C++ without the C runtime. Windows provides the network, Direct3D and the fonts.";
		float tall = surface.paragraph_height ( credits, render::FONT_BODY, wide );

		surface.paragraph ( credits, render::FONT_BODY, render::box { x, y, wide, tall + 4.0f }, colors.muted );
		y += tall + 40.0f;

		scroll_limit [ SCROLL_HELP ] = base::max ( 0.0f, y - area.bottom ( ) );
		hit_offset = 0.0f;
		hit_top = -1e9f;
		hit_bottom = 1e9f;
		hit_scroller = -1;
		surface.pop_shift ( );

	}

}

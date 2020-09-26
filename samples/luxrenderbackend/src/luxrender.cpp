#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/case_conv.hpp>

#include "luxrays/utils/oclerror.h"
#include "luxcore/luxcore.h"

// available listeners
#include "StandaloneListener.h"
#include "TCPListener.h"
#include "PipeListener.h"

using namespace std;
using namespace luxrays;
using namespace luxcore;

enum ListenerTypes { Standalone, Listener_TCP, Listener_Pipe };

ListenerTypes listenerType = ListenerTypes::Standalone;
string portName = "5736"; // port for TCPListener
string pipeName = "\\\\.\\pipe\\vsc-luxrender-pipe"; // pipe name for PipeListener
string connectionId; // portName or pipeName depends on listenerType

bool isTerminated = false;
RenderConfig* config = NULL;
RenderSession* session = NULL;
ConnectionService* listener = NULL;
thread* rendererThread = NULL;

void Render()
{
	// resume saved session data (not supported yet)
	RenderState* startState = NULL;
	Film* startFilm = NULL;

	session = RenderSession::Create(config, startState, startFilm);

	const unsigned int haltTime = config->GetProperty("batch.halttime").Get<unsigned int>();
	const unsigned int haltSpp = config->GetProperty("batch.haltspp").Get<unsigned int>();

	// Start the rendering
	session->Start();

	const Properties &stats = session->GetStats();
	while (!session->HasDone() && !isTerminated) {
		boost::this_thread::sleep(boost::posix_time::millisec(1000));
		session->UpdateStats();

		const double elapsedTime = stats.Get("stats.renderengine.time").Get<double>();
		const unsigned int pass = stats.Get("stats.renderengine.pass").Get<unsigned int>();
		// Convergence test is update inside UpdateFilm()
		const float convergence = stats.Get("stats.renderengine.convergence").Get<float>();

		// Print some information about the rendering progress
		LC_LOG(boost::str(boost::format("[Elapsed time: %3d/%dsec][Samples %4d/%d][Convergence %f%%][Avg. samples/sec % 3.2fM on %.1fK tris]") %
			int(elapsedTime) % int(haltTime) % pass % haltSpp % (100.f * convergence) %
			(stats.Get("stats.renderengine.total.samplesec").Get<double>() / 1000000.0) %
			(stats.Get("stats.dataset.trianglecount").Get<double>() / 1000.0)));
	}

	// Stop the rendering
	if (session->IsStarted())
	{
		session->Stop();
	}

	// save file
	const string renderEngine = config->GetProperty("renderengine.type").Get<string>();
	if (renderEngine != "FILESAVER") {
		// Save the rendered image
		session->GetFilm().SaveOutputs();
	}

	printf("Render session complete\n");

	// notify client
	if (!isTerminated)
	{
		listener->Send("render-session-complete");
		listener->NotifyRenderSessionComplete();
	}
}

void DeleteSession()
{
	if (session && session->IsStarted())
	{
		session->Stop();
	}

	if (rendererThread)
	{
		rendererThread->join();
		delete rendererThread;

		rendererThread = NULL;
	}

	if (session)
	{
		delete session;
		session = NULL;
	}

	if (config)
	{
		delete config;
		config = NULL;
	}
}

void messageThreadProc()
{
	luxcore::Init();
	
	int result = listener->StartListening(connectionId);
	if (result != 0)
	{
		// error
		exit(result);
	}

	vector<string> bufferInbox;
	while (!isTerminated)
	{
		bufferInbox.clear();
		if (listener->GetReceivedData(bufferInbox))
		{
			for (string line : bufferInbox)
			{
				string command;
				string args;

				int spacePos = line.find(' ');
				if (spacePos > -1)
				{
					command = line.substr(0, spacePos);
					args = line.substr(spacePos + 1, line.length() - spacePos - 1);
				}
				else
				{
					command = line;
					args = "";
				}

				if (command == "render")
				{
					if (args.empty())
					{
						printf(".cfg file name required\n");
						continue;
					}

					if (!boost::filesystem::exists(args))
					{
						printf("file not found %s\n", args);
						continue;
					}

					// delete prev session or stop current session
					DeleteSession();

					// change working directory to .cfg location
					auto path = boost::filesystem::path(args).parent_path();
					boost::filesystem::current_path(path);

					try
					{
						config = RenderConfig::Create(Properties(args));
					}
					catch (exception& e)
					{
						printf(e.what());
						printf("\n");
					}
						
					if (config)
					{
						// start rendering session
						rendererThread = new thread(Render);
					}
				}
				else if (command == "get-rendered-image")
				{
					if (!session)
					{
						printf("There is no render session\n");
						continue;
					}
					
					// only OUTPUT_RGBA_IMAGEPIPELINE is supported
					if (session->GetFilm().HasOutput(Film::OUTPUT_RGBA_IMAGEPIPELINE))
					{
						u_int outputSize = session->GetFilm().GetOutputSize(Film::OUTPUT_RGBA_IMAGEPIPELINE) * 4;
						float* buffer = (float*)malloc(outputSize);
						session->GetFilm().GetOutput<float>(Film::OUTPUT_RGBA_IMAGEPIPELINE, buffer);

						listener->Send((char*)buffer, 0, outputSize);

						free(buffer);
					}
				}
				else if (command == "exit")
				{
					// exit
					isTerminated = true;
					DeleteSession();
					listener->StopListening();
					delete listener;
					exit(EXIT_SUCCESS);
					return;
				}
				else
				{
					printf("Unknown command: %s\n", command);
				}
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	listener->StopListening();
}

int main(int argc, char *argv[])
{
	if (argc == 2)
	{
		// standalone mode
		// config to render (.cfg)
		connectionId = argv[1];
		listenerType = ListenerTypes::Standalone;
	}
	else if (argc > 2)
	{
		// -port
		if (string(argv[1]) == "-port")
		{
			if (argc > 2)
			{
				u_int port;
				try
				{
					port = atoi(argv[2]);
					if (port > 0 && port < 65535)
					{
						portName = to_string(port);
						listenerType = ListenerTypes::Listener_TCP;
					}
					else
					{
						printf("Invalid port (must be in range (0..65535))\n");
						exit(1);
					}
				}
				catch(exception e)
				{
					printf("Invalid port\n");
					exit(1);
				}
			}
			else
			{
				printf("Port required\n");
				exit(1);
			}
		}

		// -pipe
		if (string(argv[1]) == "-pipe")
		{
			if (argc > 2)
			{
				pipeName = "\\\\.\\pipe\\" + string(argv[2]);
				listenerType = ListenerTypes::Listener_Pipe;
			}
		}
	}

	try
	{
		// select listener type
		switch (listenerType)
		{
		case ListenerTypes::Standalone:
			listener = new StandaloneListener();
			break;
		case ListenerTypes::Listener_TCP:
			listener = new TCPListener();
			connectionId = portName;
			break;
		case ListenerTypes::Listener_Pipe:
			listener = new PipeListener();
			connectionId = pipeName;
			break;
		}

		thread messageThread = thread(messageThreadProc);

		char input[100];
		while (!isTerminated)
		{
			cin.getline(input, sizeof(input));

			string inputStr = string(input);
			if (inputStr == "q" || inputStr == "quit" || inputStr == "e" || inputStr == "exit")
			{
				isTerminated = true;
				break;
			}
			else
			{
				listener->AddReceivedData(inputStr);
			}
		}

		messageThread.join();
		delete listener;
		DeleteSession();

		LC_LOG("Done.");
	} catch (runtime_error &err) {
		LC_LOG("RUNTIME ERROR: " << err.what());
		return EXIT_FAILURE;
	} catch (exception &err) {
		LC_LOG("ERROR: " << err.what());
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

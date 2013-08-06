#include "main.h"

void _out(string str, EventHandler::OUT_TYPE type);

int main(int argc, char ** argv)
{
	
	std::cout << "Battlefield 3 TuxNet (Rcon Service) Rev. " << REVISION << " :: Copyright 2009-2013 (c) Plain-Solution.de" << endl;

	try
	{
		// settings.ini lesen
		boost::property_tree::ptree pt;
		boost::property_tree::ini_parser::read_ini(string(argv[0]).append(".ini"), pt);

		std::cout << "BF3 Server: " << pt.get<string>("Server.Host") << ":" << pt.get<string>("Server.Port") << endl;
		////

		fstream pidfile(string(argv[0]).append(".pid"), ios::out);
		if(!pidfile)
			throw string("Could not write pidfile");
		pidfile << getpid() << endl;
		pidfile.close();

		RconConnection * rcon = new RconConnection( pt.get<string>("Server.Host"), pt.get<string>("Server.Port"), pt.get<string>("Server.Password") );

		rcon->Login();
		rcon->EnableEvents();

		string msg = "[BF3 TuxNet Rev. ";
		msg += REVISION;
		msg += "] by Plain-Solution.de";
		rcon->sendRequest(createWords("admin.say", msg.c_str(), "all"));
		
		EventHandler * eventHandler = new EventHandler(rcon, &pt);

		thread eventThread([eventHandler]() {
			eventHandler->DoWork();
		});


		while(true)
		{
			try
			{
				TextRconPacket response = rcon->getResponse();

				if(eventHandler->needResponse != 0 && response.isValid() && response.m_isResponse && eventHandler->needResponse == response.m_sequence)
				{
					eventHandler->response = new TextRconPacket(response);
					eventHandler->needResponse = 0;
				}
				if(response.isValid() && !response.m_isResponse && response.m_originatedOnServer)
					eventHandler->AddEvent(response);

				}
			catch(string e)
			{
				eventHandler->out(e, EventHandler::OUT_TYPE::ERR);
			}

		}
		eventHandler->Disable();
		eventThread.join();
	}
	catch(exception & e)
	{
		_out(e.what(), EventHandler::OUT_TYPE::ERR);
	}
	catch(string & e)
	{
		_out(e, EventHandler::OUT_TYPE::ERR);
	}

	remove(string(argv[0]).append(".pid").c_str());

	return 0;
}

boost::system::error_code sendRequest(tcp::socket & socket, uint32_t sequence, Words words)
{
	boost::system::error_code error;

	TextRconPacket textRequest(false, false, sequence, words);

	BinaryRconPacket binaryRequest(textRequest);

	const uint8_t* requestData;
	unsigned int requestLength;
	binaryRequest.getBuffer(requestData, requestLength);

	socket.write_some(boost::asio::buffer(reinterpret_cast<const char*>(requestData), requestLength), error);

	return error;	
}

TextRconPacket getResponse(tcp::socket & socket)
{
	boost::array<char, 16384> buf;
	uint8_t responseHeaderBuf[BinaryRconPacketHeader::Size];
	
	size_t len = socket.read_some(boost::asio::buffer(buf));

	for(uint8_t pos = 0; pos < BinaryRconPacketHeader::Size; pos++)
		responseHeaderBuf[pos] = buf[pos];

	BinaryRconPacketHeader binaryRconPacketHeader(responseHeaderBuf);

	if(!binaryRconPacketHeader.isValid())
		throw string("Invalid Binary Packet Header");
	
	uint32_t binaryRconResponsePacketSize = binaryRconPacketHeader.getPacketSize();
	uint32_t binaryRconResponseBodySize = binaryRconResponsePacketSize - BinaryRconPacketHeader::Size;

	uint8_t* responseBodyBuf = new uint8_t[binaryRconResponseBodySize];

	for(uint32_t pos = 0; pos < binaryRconResponseBodySize; pos++)
		responseBodyBuf[pos] = buf[pos + BinaryRconPacketHeader::Size];

	BinaryRconPacket binaryResponse(binaryRconPacketHeader, responseBodyBuf);

	if(!binaryResponse.isValid())
		throw string("Invalid Binary Packet");

	return TextRconPacket(binaryResponse);
}

void _out(string str, EventHandler::OUT_TYPE type)
{
	time_t now = time(0);
	std::tm * time = localtime(&now);
	char * tstr = new char[34];
	sprintf(tstr, "[%02d.%02d.%04d %02d:%02d:%02d] ", time->tm_mday, time->tm_mon + 1, 1900 + time->tm_year, time->tm_hour,
	  time->tm_min, time->tm_sec);

	cout << tstr;

	switch(type)
	{
	case EventHandler::OUT_TYPE::DEBUG:
		cout << "[DEBUG] ";
		break;
	case EventHandler::OUT_TYPE::ERR:
		cout << "[ERROR] ";
		break;
	case EventHandler::OUT_TYPE::INFO:
		cout << "[INFO] ";
		break;
	case EventHandler::OUT_TYPE::WARNING:
		cout << "[WARNING] ";
		break;
	default:
		break;
	}

	cout << str << endl;
}
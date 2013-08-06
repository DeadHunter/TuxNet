#ifndef RCON_CONNECTION_H
#define RCON_CONNECTION_H

#include <string>

#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

#include "md5.h"
#include "RconPacket.h"

using boost::asio::ip::tcp;
using namespace std;

class RconConnection
{
private:
	string host;
	string port;
	string password;

	uint32_t sequence;

	boost::asio::io_service io_service;
	tcp::socket * socket;

public:
	RconConnection(string host, string port, string password);
	~RconConnection(void);

	void Login(void);
	void EnableEvents(void);

	uint32_t getSequence();

	boost::system::error_code sendRequest(Words words);
	TextRconPacket getResponse();
};

#endif

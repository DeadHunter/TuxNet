#include "RconConnection.h"
#pragma warning(disable: 4996)


RconConnection::RconConnection(string host, string port, string password)
	: host(host), port(port), password(password)
{
	this->sequence = 1;

	boost::system::error_code error;
	this->socket = new tcp::socket(this->io_service);
	socket->open(tcp::v4(), error);

	if(!error)
	{

		tcp::resolver resolver(io_service);
		tcp::resolver::query query(tcp::v4(), this->host, this->port );
		tcp::resolver::iterator iterator = resolver.resolve(query);

		socket->connect(*iterator);

	}

}


RconConnection::~RconConnection(void)
{
}


void RconConnection::Login(void)
{
	Words loginCommand;
	loginCommand.push_back("login.hashed");

	if(sendRequest(loginCommand))
		throw string("Login failed");

	TextRconPacket response = getResponse();

	if(response.m_isResponse && response.m_data[0] == "OK")
	{
		string salt = response.m_data[1];

		const char* hex_str = salt.c_str();
		string hash, saltHex;
		uint32_t ch;

		for( ; sscanf( hex_str, "%2x", &ch) == 1 ; hex_str += 2)
			hash += ch;

		saltHex = hash;

		hash.append( this->password );
		hash = MD5String( (char*)hash.c_str() );

		boost::to_upper(hash);

		loginCommand.clear();
		loginCommand.push_back("login.hashed");
		loginCommand.push_back(hash);

		if(sendRequest(loginCommand))
			throw string("sendRequest failed :: Login");

		response = getResponse();

		if(response.m_isResponse && response.m_data[0] == "InvalidPasswordHash")
			throw string("Login failed :: InvalidPasswordHash (Salt: " + salt + " | SaltHex: "+ saltHex +" | Hash: " + hash + ")");

	}
	else
		throw string("Login failed");


}

void RconConnection::EnableEvents(void)
{
	Words command;
	command.clear();
	command.push_back("admin.eventsEnabled");
	command.push_back("true");

	if(sendRequest(command))
		throw string("sendRequest failed :: eventsEnabled");

	TextRconPacket response = getResponse();

	if(!response.m_isResponse || response.m_data[0] != "OK")
		throw string("eventsEnabled failed");
}

boost::system::error_code RconConnection::sendRequest(Words words)
{
	boost::system::error_code error;

	TextRconPacket textRequest(false, false, this->sequence, words);
	BinaryRconPacket binaryRequest(textRequest);

	const uint8_t* requestData;
	unsigned int requestLength;
	binaryRequest.getBuffer(requestData, requestLength);

	this->socket->write_some(boost::asio::buffer(requestData, requestLength), error);

	this->sequence++;

	return error;
}

TextRconPacket RconConnection::getResponse()
{
	boost::array<char, 16384> buf;
	uint8_t responseHeaderBuf[BinaryRconPacketHeader::Size];
	
	size_t len = this->socket->read_some(boost::asio::buffer(buf));

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

uint32_t RconConnection::getSequence()
{
	return this->sequence;
}
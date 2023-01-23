
#pragma once

struct addrinfo;

class CGIManager
{
public:
	CGIManager(const String& rHostname, U16 port);
	~CGIManager();

	void Add(const String& rCGI);

	void Process();

private:

	const String	mHostname;
	const U16		mPort;

	addrinfo*		mpAddrInfo = nullptr;

	Vector<String>	mQueue;
	std::mutex		mQueueMutex;
};
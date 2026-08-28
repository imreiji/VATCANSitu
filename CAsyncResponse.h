#pragma once
#include <string>

class CAsyncResponse
{
public: 
	std::string reponseMessage;

	// Which handler the message is displayed under - see ASYNC_MESSAGE_* in constants.h.
	// Initialised because OnRefresh now branches on it, and a default constructed
	// response used to carry whatever was on the stack.
	int responseCode{ 0 };
};


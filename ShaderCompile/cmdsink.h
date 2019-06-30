//====== Copyright © 1996-2006, Valve Corporation, All rights reserved. =======//
//
// Purpose: Command sink interface implementation.
//
// $NoKeywords: $
//
//=============================================================================//

#ifndef CMDSINK_H
#define CMDSINK_H
#ifdef _WIN32
#pragma once
#endif

namespace CmdSink
{

/*

struct IResponse

Interface to give back command execution results.

*/
struct IResponse
{
	virtual ~IResponse() = default;
	virtual void Release() { delete this; }

	// Returns whether the command succeeded
	virtual bool Succeeded() = 0;

	// If the command succeeded returns the result buffer length, otherwise zero
	virtual size_t GetResultBufferLen() = 0;
	// If the command succeeded returns the result buffer base pointer, otherwise NULL
	virtual const void* GetResultBuffer() = 0;

	// Returns a zero-terminated string of messages reported during command execution, or NULL if nothing was reported
	virtual const char* GetListing() = 0;
};

/*

Response implementation when the result is a generic error.

*/
class CResponseError : public IResponse
{
public:
	explicit CResponseError() = default;
	~CResponseError() override = default;

public:
	bool Succeeded() override { return false; }

	size_t GetResultBufferLen() override { return 0; }
	const void* GetResultBuffer() override { return nullptr; }

	const char* GetListing() override { return nullptr; }
};

}; // namespace CmdSink


#endif // #ifndef CMDSINK_H
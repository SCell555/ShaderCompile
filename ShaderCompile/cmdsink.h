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
class IResponse
{
protected:
	virtual ~IResponse() = default;
public:
	void Release() noexcept { delete this; }

	// Returns whether the command succeeded
	virtual bool Succeeded() const = 0;

	// If the command succeeded returns the result buffer length, otherwise zero
	virtual size_t GetResultBufferLen() const = 0;
	// If the command succeeded returns the result buffer base pointer, otherwise NULL
	virtual const void* GetResultBuffer() const = 0;

	// Returns a zero-terminated string of messages reported during command execution, or NULL if nothing was reported
	virtual const char* GetListing() const = 0;
};

}; // namespace CmdSink

#endif // #ifndef CMDSINK_H
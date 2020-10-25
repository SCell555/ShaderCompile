
#pragma once

module Compiler
{
	struct CompilerDefine
	{
		string name;
		int value;
	}
	sequence<CompilerDefine> Defines;

	sequence<byte> Data;
	struct CompilerOutput
	{
		Data bytecode;
		Data error;
	}

	interface D3DCompiler
	{
		void AddInclude(string path, string internalName);
		CompilerOutput Compile( string fileName, string shaderVersion, Defines defines, int flags );
		void Shutdown();
	}
}
#include <filecheck.hpp>
#include <main.hpp>
#include <cstdint>
#include <string>
#include <symbolfinder.hpp>
#include <detours.h>
#include <networkstringtabledefs.h>
#include <strtools.h>

namespace filecheck
{

#if defined _WIN32

static const char *IsValidFileForTransfer_sig = "\x55\x8B\xEC\x8B\x4D\x08\x85\xC9\x0F\x84\x2A\x2A\x2A\x2A\x80\x39";
static size_t IsValidFileForTransfer_siglen = 16;

#elif defined __linux

static const char *IsValidFileForTransfer_sig = "@_ZN8CNetChan22IsValidFileForTransferEPKc";
static size_t IsValidFileForTransfer_siglen = 0;

#elif defined __APPLE__

static const char *IsValidFileForTransfer_sig = "@__ZN8CNetChan22IsValidFileForTransferEPKc";
static size_t IsValidFileForTransfer_siglen = 0;

#endif

typedef bool( *IsValidFileForTransfer_t )( const char *file );

static IsValidFileForTransfer_t IsValidFileForTransfer = nullptr;
static MologieDetours::Detour<IsValidFileForTransfer_t> *IsValidFileForTransfer_detour = nullptr;

static INetworkStringTable *downloads = nullptr;
static const char *downloads_dir = "downloads" CORRECT_PATH_SEPARATOR_S;

inline bool BlockDownload( const char *filepath )
{
	DebugWarning( "[ServerSecure] Blocking download of \"%s\"\n", filepath );
	return false;
}

static bool IsValidFileForTransfer_d( const char *filepath )
{
	if( filepath == nullptr )
	{
		DebugWarning( "[ServerSecure] Invalid file to download (string pointer was NULL)\n" );
		return false;
	}

	size_t len = strlen( filepath );
	if( len == 0 )
	{
		DebugWarning( "[ServerSecure] Invalid file to download (path length was 0)\n" );
		return false;
	}

	std::string nicefile( filepath, len );
	if( !V_RemoveDotSlashes( &nicefile[0] ) )
		return BlockDownload( filepath );

	len = strlen( nicefile.c_str( ) );
	nicefile.resize( len );
	filepath = nicefile.c_str( );

	DebugWarning( "[ServerSecure] Checking file \"%s\"\n", filepath );

	if( !IsValidFileForTransfer( filepath ) )
		return BlockDownload( filepath );

	int32_t index = downloads->FindStringIndex( filepath );
	if( index != INVALID_STRING_INDEX )
		return true;

	if( len == 22 && strncmp( filepath, downloads_dir, 10 ) == 0 && strncmp( filepath + len - 4, ".dat", 4 ) == 0 )
		return true;

	return BlockDownload( filepath );
}

LUA_FUNCTION_STATIC( EnableFileValidation )
{
	LUA->CheckType( 1, GarrysMod::Lua::Type::BOOL );

	bool detour = LUA->GetBool( 1 );
	if( detour && IsValidFileForTransfer_detour == nullptr )
	{
		IsValidFileForTransfer_detour = new( std::nothrow ) MologieDetours::Detour<IsValidFileForTransfer_t>(
			IsValidFileForTransfer,
			IsValidFileForTransfer_d
		);
		LUA->PushBool( IsValidFileForTransfer_detour != nullptr );
	}
	else if( !detour && IsValidFileForTransfer_detour != nullptr )
	{
		delete IsValidFileForTransfer_detour;
		IsValidFileForTransfer_detour = nullptr;
		LUA->PushBool( true );
	}
	else
	{
		LUA->PushBool( false );
	}

	return 1;
}

void Initialize( lua_State *state )
{
	INetworkStringTableContainer *networkstringtable = global::engine_loader.GetInterface<INetworkStringTableContainer>(
		INTERFACENAME_NETWORKSTRINGTABLESERVER
	);
	if( networkstringtable == nullptr )
		LUA->ThrowError( "unable to get INetworkStringTableContainer" );

	downloads = networkstringtable->FindTable( "downloadables" );
	if( downloads == nullptr )
		LUA->ThrowError( "missing \"downloadables\" string table" );

	SymbolFinder symfinder;
	IsValidFileForTransfer = reinterpret_cast<IsValidFileForTransfer_t>( symfinder.ResolveOnBinary(
		global::engine_lib.c_str( ),
		IsValidFileForTransfer_sig,
		IsValidFileForTransfer_siglen
	) );
	if( IsValidFileForTransfer == nullptr )
		LUA->ThrowError( "unable to sigscan for CNetChan::IsValidFileForTransfer" );

	LUA->PushCFunction( EnableFileValidation );
	LUA->SetField( -2, "EnableFileValidation" );
}

void Deinitialize( lua_State * )
{
	if( IsValidFileForTransfer != nullptr )
	{
		delete IsValidFileForTransfer_detour;
		IsValidFileForTransfer_detour = nullptr;
	}
}

}

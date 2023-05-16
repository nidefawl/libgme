/* How to play game music files with Music_Player (requires SDL library)

Run program with path to a game music file.

Left/Right  Change track
Space       Pause/unpause
E           Normal/slight stereo echo/more stereo echo
A			Enable/disable accurate emulation
-/=         Adjust tempo
1-9         Toggle channel on/off
0           Reset tempo and turn channels back on */

#include <SDL_keycode.h>

#include "Music_Player.h"
#include "Audio_Scope.h"

#include <cstring>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "SDL.h"

void handle_error( const char* );

int const scopeWidth = 512;
int const scopeHeight = 256;
int const sample_rate = 192000;

static bool paused;
static Audio_Scope* scope;
static Music_Player* player;
static short scope_buf [scopeWidth * 2];
//The window we'll be rendering to
SDL_Window* gWindow = NULL;

//The surface contained by the window
SDL_Surface* gScreenSurface = NULL;

static void init()
{
	// Start SDL
	if ( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_AUDIO ) < 0 )
	{
		printf( "SDL could not initialize! SDL Error: %s\n", SDL_GetError() );
		exit( EXIT_FAILURE );
	}
	atexit( SDL_Quit );
	
	gWindow = SDL_CreateWindow( "<3", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, scopeWidth, scopeHeight, SDL_WINDOW_SHOWN );
	if( gWindow == NULL )
	{
		printf( "Window could not be created! SDL Error: %s\n", SDL_GetError() );
		exit( EXIT_FAILURE );
	}
	SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

	//Get window surface
	gScreenSurface = SDL_GetWindowSurface( gWindow );

	
	// Init scope
	scope = new Audio_Scope(gScreenSurface);
	if ( !scope )
		handle_error( "Out of memory" );
	if ( scope->init( scopeWidth, scopeHeight ) )
		handle_error( "Couldn't initialize scope" );
	memset( scope_buf, 0, sizeof scope_buf );
	
	// Create player
	player = new Music_Player;
	if ( !player )
		handle_error( "Out of memory" );
	handle_error( player->init(192000) );
	player->set_scope_buffer( scope_buf, scopeWidth * 2 );
}

static void start_track( int track, const char* path )
{
	paused = false;
	handle_error( player->start_track( track - 1 ) );
	
	// update window title with track info
	
	long seconds = player->track_info().length / 1000;
	const char* game = player->track_info().game;
	if ( !*game )
	{
		// extract filename
		game = strrchr( path, '\\' ); // DOS
		if ( !game )
			game = strrchr( path, '/' ); // UNIX
		if ( !game )
			game = path;
		else
			game++; // skip path separator
	}
	
	char title [512];
	sprintf( title, "%s: %d/%d %s (%ld:%02ld)",
			game, track, player->track_count(), player->track_info().song,
			seconds / 60, seconds % 60 );
	SDL_SetWindowTitle( gWindow, title );
	SDL_FillRect(gScreenSurface, NULL, 0x000000);
}

int main( int argc, char** argv )
{
	init();
	
	// Load file
	const char* argPath = (argc > 1 ? argv [argc - 1] : "test.nsf");
	char* path = strdup(argPath);
	handle_error( player->load_file( path ) );
	start_track( 1, path );
	
	// Main loop
	int track = 1;
	double tempo = 1.0;
	bool running = true;
	double stereo_depth = 1.0;
	bool accurate = false;
	int muting_mask = 0;

	while ( running )
	{
		SDL_Delay( 5 );
		
		// Update scope
		scope->draw( scope_buf, scopeWidth, 2 );
		//Update the surface
		SDL_UpdateWindowSurface( gWindow );
		
		// Automatically go to next track when current one ends
		if ( player->track_ended() )
		{
			if ( track < player->track_count() )
				start_track( ++track, path );
			else
				player->pause( paused = true );
		}
		
		// Handle keyboard input
		SDL_Event e;
		while ( SDL_PollEvent( &e ) )
		{
			switch ( e.type )
			{
			case SDL_QUIT:
				running = false;
				break;
			case (SDL_DROPFILE): {      // In case if dropped file
				char* dropped_filedir = e.drop.file;
				if (dropped_filedir) {
					free(path);
					path = strdup(dropped_filedir);
					handle_error( player->load_file( path ) );
				}
				start_track( 1, path );
				// Shows directory of dropped file
				SDL_free(dropped_filedir);    // Free dropped_filedir memory
				break;
			}
			
			case SDL_KEYDOWN:
				int key = e.key.keysym.sym;
				// printf("Key %d state %d\n", key, e.key.repeat);
				switch ( key )
				{
				case SDLK_q:
				case SDLK_ESCAPE: // quit
					running = false;
					break;
				
				case SDLK_LEFT: // prev track
					if ( !paused && !--track )
						track = 1;
					start_track( track, path );
					break;
				
				case SDLK_RIGHT: // next track
					if ( track < player->track_count() )
						start_track( ++track, path );
					break;
				
				case SDLK_KP_MINUS: // reduce tempo
					tempo -= 0.1;
					if ( tempo < 0.1 )
						tempo = 0.1;
					player->set_tempo( tempo );
					break;
				
				case SDLK_KP_PLUS: // increase tempo
					tempo += 0.1;
					if ( tempo > 2.0 )
						tempo = 2.0;
					player->set_tempo( tempo );
					break;
				
				case SDLK_SPACE: // toggle pause
					paused = !paused;
					player->pause( paused );
					break;
				
				case SDLK_a: // toggle accurate emulation
					accurate = !accurate;
					player->enable_accuracy( accurate );
					break;
				
				case SDLK_e: // toggle echo
					stereo_depth += 0.2;
					if ( stereo_depth > 0.5 )
						stereo_depth = 0;
					player->set_stereo_depth( stereo_depth );
					break;
				
				case SDLK_0: // reset tempo and muting
					tempo = 1.0;
					muting_mask = 0;
					player->set_tempo( tempo );
					player->mute_voices( muting_mask );
					break;
				
				default:
					if ( SDLK_1 <= key && key <= SDLK_9 ) // toggle muting
					{
						muting_mask ^= 1 << (key - SDLK_1);
						player->mute_voices( muting_mask );
					}
				}
			}
		}
	}
	
	// Cleanup
	delete player;
	delete scope;
	free(path);

	//Destroy window
	SDL_DestroyWindow( gWindow );
	gWindow = NULL;

	//Quit SDL subsystems
	SDL_Quit();
	
	return 0;
}

void handle_error( const char* error )
{
	if ( error )
	{
		// put error in window title
		char str [256];
		sprintf( str, "Error: %s", error );
		fprintf( stderr, "%s\n", str );
		// SDL_WM_SetCaption( str, str );
		
		// wait for keyboard or mouse activity
		SDL_Event e;
		do
		{
			while ( !SDL_PollEvent( &e ) ) { }
		}
		while ( e.type != SDL_QUIT && e.type != SDL_KEYDOWN && e.type != SDL_MOUSEBUTTONDOWN );

		exit( EXIT_FAILURE );
	}
}

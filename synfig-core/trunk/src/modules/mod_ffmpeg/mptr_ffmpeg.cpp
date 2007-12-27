/* === S Y N F I G ========================================================= */
/*!	\file mptr_ffmpeg.cpp
**	\brief ppm Target Module
**
**	$Id$
**
**	\legal
**	Copyright (c) 2002-2005 Robert B. Quattlebaum Jr., Adrian Bentley
**
**	This package is free software; you can redistribute it and/or
**	modify it under the terms of the GNU General Public License as
**	published by the Free Software Foundation; either version 2 of
**	the License, or (at your option) any later version.
**
**	This package is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
**	General Public License for more details.
**	\endlegal
**
** === N O T E S ===========================================================
**
** ========================================================================= */

/* === H E A D E R S ======================================================= */

#ifdef USING_PCH
#	include "pch.h"
#else
#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include <ETL/stringf>
#include "mptr_ffmpeg.h"
#include <stdio.h>
#include <sys/types.h>
#if HAVE_SYS_WAIT_H
 #include <sys/wait.h>
#endif
#if HAVE_IO_H
 #include <io.h>
#endif
#if HAVE_PROCESS_H
 #include <process.h>
#endif
#if HAVE_FCNTL_H
 #include <fcntl.h>
#endif
#include <unistd.h>
#include <iostream>
#include <algorithm>
#include <functional>
#include <ETL/stringf>
#endif

/* === M A C R O S ========================================================= */

using namespace synfig;
using namespace std;
using namespace etl;

#if defined(HAVE_FORK) && defined(HAVE_PIPE) && defined(HAVE_WAITPID)
 #define UNIX_PIPE_TO_PROCESSES
#elif defined(HAVE__SPAWNLP) && defined(HAVE__PIPE) && defined(HAVE_CWAIT)
 #define WIN32_PIPE_TO_PROCESSES
#endif

/* === G L O B A L S ======================================================= */

SYNFIG_IMPORTER_INIT(ffmpeg_mptr);
SYNFIG_IMPORTER_SET_NAME(ffmpeg_mptr,"ffmpeg");
SYNFIG_IMPORTER_SET_EXT(ffmpeg_mptr,"avi");
SYNFIG_IMPORTER_SET_VERSION(ffmpeg_mptr,"0.1");
SYNFIG_IMPORTER_SET_CVS_ID(ffmpeg_mptr,"$Id$");

/* === M E T H O D S ======================================================= */

bool
ffmpeg_mptr::seek_to(int frame)
{
	if(frame<cur_frame || !file)
	{
		if(file)
		{
			fclose(file);
			int status;
#if defined(WIN32_PIPE_TO_PROCESSES)
		cwait(&status,pid,0);
#elif defined(UNIX_PIPE_TO_PROCESSES)
		waitpid(pid,&status,0);
#endif
		}

#if defined(WIN32_PIPE_TO_PROCESSES)

	int p[2];
	int stdin_fileno, stdout_fileno;

	if(_pipe(p, 512, O_BINARY | O_NOINHERIT) < 0) {
		cerr<<"Unable to open pipe to ffmpeg"<<endl;
		return false;
	}

	// Save stdin/stdout so we can restore them later
	stdin_fileno  = _dup(_fileno(stdin));
	stdout_fileno = _dup(_fileno(stdout));

	// ffmpeg should write to the pipe
	if(_dup2(p[1], _fileno(stdout)) != 0) {
		cerr<<"Unable to open pipe to ffmpeg"<<endl;
		return false;
	}

	/*
	ffmpeg accepts the input filename on the command-line
	if(_dup2(_fileno(input), _fileno(stdin)) != 0) {
		synfig::error(_("Unable to open pipe to ffmpeg"));
		return false;
	}
	*/

	pid = _spawnlp(_P_NOWAIT, "ffmpeg", "ffmpeg", "-i", filename.c_str(), "-an", "-f", "image2pipe", "-vcodec", "ppm", "-", (const char *)NULL);
	if( pid < 0) {
		cerr<<"Unable to open pipe to ffmpeg"<<endl;
		return false;
	}

	// Restore stdin/stdout
	if(_dup2(stdin_fileno, _fileno(stdin)) != 0) {
		cerr<<"Unable to open pipe to ffmpeg"<<endl;
		return false;
	}
	if(_dup2(stdout_fileno, _fileno(stdout)) != 0) {
		cerr<<"Unable to open pipe to ffmpeg"<<endl;
		return false;
	}
	close(stdin_fileno);
	close(stdout_fileno);

	// Close the pipe write end - ffmpeg uses it
	close(p[1]);
	
	// We read data from the read end of the pipe
	file = fdopen(p[0], "rb");

#elif defined(UNIX_PIPE_TO_PROCESSES)

		int p[2];
	  
		if (pipe(p)) {
			cerr<<"Unable to open pipe to ffmpeg"<<endl;
			return false;
		};
	  
		pid = fork();
	  
		if (pid == -1) {
			cerr<<"Unable to open pipe to ffmpeg"<<endl;
			return false;
		}
	  
		if (pid == 0){
			// Child process
			// Close pipein, not needed
			close(p[0]);
			// Dup pipein to stdout
			if( dup2( p[1], STDOUT_FILENO ) == -1 ){
				cerr<<"Unable to open pipe to ffmpeg"<<endl;
				return false;
			}
			// Close the unneeded pipein
			close(p[1]);
			execlp("ffmpeg", "ffmpeg", "-i", filename.c_str(), "-an", "-f", "image2pipe", "-vcodec", "ppm", "-", (const char *)NULL);
			// We should never reach here unless the exec failed
			cerr<<"Unable to open pipe to ffmpeg"<<endl;
			return false;
		} else {
			// Parent process
			// Close pipeout, not needed
			close(p[1]);
			// Save pipein to file handle, will read from it later
			file = fdopen(p[0], "rb");
		}

#else
	#error There are no known APIs for creating child processes
#endif

		if(!file)
		{
			cerr<<"Unable to open pipe to ffmpeg"<<endl;
			return false;
		}
		cur_frame=-1;
	}

	while(cur_frame<frame-1)
	{
		cerr<<"Seeking to..."<<frame<<'('<<cur_frame<<')'<<endl;
		if(!grab_frame())
			return false;
	}
	return true;
}

bool
ffmpeg_mptr::grab_frame(void)
{
	if(!file)
	{
		cerr<<"unable to open "<<filename<<endl;
		return false;
	}
	int w,h;
	float divisor;
	char cookie[2];
	cookie[0]=fgetc(file);
	cookie[1]=fgetc(file);

	if(cookie[0]!='P' || cookie[1]!='6')
	{
		cerr<<"stream not in PPM format \""<<cookie[0]<<cookie[1]<<'"'<<endl;
		return false;
	}

	fgetc(file);
	fscanf(file,"%d %d\n",&w,&h);
	fscanf(file,"%f",&divisor);
	fgetc(file);

	if(feof(file))
		return false;

	int x;
	int y;
	frame.set_wh(w,h);
	for(y=0;y<frame.get_h();y++)
		for(x=0;x<frame.get_w();x++)
		{
			if(feof(file))
				return false;
/*
			frame[y][x]=Color(
				(float)(unsigned char)fgetc(file)/divisor,
				(float)(unsigned char)fgetc(file)/divisor,
				(float)(unsigned char)fgetc(file)/divisor,
				1.0
*/
			float r=gamma().r_U8_to_F32((unsigned char)fgetc(file));
			float g=gamma().g_U8_to_F32((unsigned char)fgetc(file));
			float b=gamma().b_U8_to_F32((unsigned char)fgetc(file));
			frame[y][x]=Color(
				r,
				g,
				b,
				1.0
			);
		}
	cur_frame++;
	return true;
}

ffmpeg_mptr::ffmpeg_mptr(const char *f)
{
	pid=-1;
#ifdef HAVE_TERMIOS_H
	tcgetattr (0, &oldtty);
#endif
	filename=f;
	file=NULL;
	fps=23.98;
	cur_frame=-1;
}

ffmpeg_mptr::~ffmpeg_mptr()
{
	if(file)
	{
		fclose(file);
		int status;
#if defined(WIN32_PIPE_TO_PROCESSES)
		cwait(&status,pid,0);
#elif defined(UNIX_PIPE_TO_PROCESSES)
		waitpid(pid,&status,0);
#endif
	}
#ifdef HAVE_TERMIOS_H
	tcsetattr(0,TCSANOW,&oldtty);
#endif
}

bool
ffmpeg_mptr::get_frame(synfig::Surface &surface,Time time, synfig::ProgressCallback *)
{
	int i=(int)(time*fps);
	if(i!=cur_frame)
	{
		if(!seek_to(i))
			return false;
		if(!grab_frame());
			return false;
	}

	surface=frame;
	return false;
}

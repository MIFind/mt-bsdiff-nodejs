/*-
 * Copyright 2003-2005 Colin Percival
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions 
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "bspatch.h"
#include "../shared.h"

static off_t offtin(u_char *buf)
{
	off_t y;

	y=buf[7]&0x7F;
	y=y<<8; y+=buf[6];
	y=y<<8; y+=buf[5];
	y=y<<8; y+=buf[4];
	y=y<<8; y+=buf[3];
	y=y<<8; y+=buf[2];
	y=y<<8; y+=buf[1];
	y=y<<8; y+=buf[0];

	if(buf[7]&0x80) 
		y = -y;

	return y;
}

static off_t readFileToBuffer(int fd, uint8_t* buffer, off_t bufferSize)
{
    off_t bytesRead = 0;
    int ret;
    while (bytesRead < bufferSize)
    {
        ret = read(fd, buffer + bytesRead, bufferSize - bytesRead);
        if (ret > 0)
        {
            bytesRead += ret;
        }
        else
        {
            break;
        }
    }
    return bytesRead;
}

static off_t writeFileFromBuffer(int fd, uint8_t* buffer, off_t bufferSize)
{
    off_t bytesWritten = 0;
    int ret;
    while (bytesWritten < bufferSize)
    {
        ret = write(fd, buffer + bytesWritten, bufferSize - bytesWritten);
        if (ret > 0)
        {
            bytesWritten += ret;
        }
        else
        {
            break;
        }
    }
    return bytesWritten;
}

int bspatch(const char* error, const char* oldfile, const char* newfile, const char* patchfile, void* progressWorker, void(*callback)(off_t, off_t, void*)) 
{
	FILE * f, * cpf, * dpf, * epf;
	BZFILE * cpfbz2, * dpfbz2, * epfbz2;
	int cbz2err, dbz2err, ebz2err;
	int fd;
	ssize_t oldsize;
	t_header header;
	u_char buf[8];
	u_char *old, *new;
	off_t oldpos,newpos;
	off_t ctrl[3];
	off_t lenread;
	off_t i;

	/* Open patch file */
	if ((f = fopen(patchfile, FOPEN_READ_FLAGS)) == NULL) 
	{
		sprintf((char*)error, "\"%s\" %s", patchfile, strerror(errno));
		return -1;
	}

	/*
	File format:
		0	8	"BSDIFF40"
		8	8	X
		16	8	Y
		24	8	sizeof(newfile)
		32	X	bzip2(control block)
		32+X	Y	bzip2(diff block)
		32+X+Y	???	bzip2(extra block)
	with control block a set of triples (x,y,z) meaning "add x bytes
	from oldfile to x bytes from the diff block; copy y bytes from the
	extra block; seek forwards in oldfile by z bytes".
	*/

	/* Read header */
	if (fread(&header, 1, 32, f) < 32) 
	{
		if (feof(f)) 
		{
			sprintf((char*)error, "\"%s\"Corrupt patch", patchfile);
			return -1;
		}
	}

	/* Check for appropriate magic */
	if (memcmp(header.magic, "BSDIFF40", 8) != 0) 
	{
		sprintf((char*)error, "\"%s\"Corrupt patch", patchfile);
		return -1;
	}		

	/* Read lengths from header */
	header.bzctrllen = offtin(&header.bzctrllen);
	header.bzdatalen = offtin(&header.bzdatalen);
	header.newsize = offtin(&header.newsize);

	/* Close patch file and re-open it via libbzip2 at the right places */
	if (fclose(f)) 
	{
		sprintf((char*)error, "\"%s\" %s", patchfile, strerror(errno));
		return -1;
	}		
	if ((cpf = fopen(patchfile, FOPEN_READ_FLAGS)) == NULL) 
	{
		sprintf((char*)error, "\"%s\" %s", patchfile, strerror(errno));
		return -1;
	}	
	if (fseek(cpf, 32, SEEK_SET)) 
	{
		sprintf((char*)error, "\"%s\" %s", patchfile, strerror(errno));
		return -1;
	}		
	if ((cpfbz2 = BZ2_bzReadOpen(&cbz2err, cpf, 0, 0, NULL, 0)) == NULL) 
	{
		sprintf((char*)error, "BZ2_bzReadOpen, bz2err = %d", cbz2err);
		return -1;
	}	
	if ((dpf = fopen(patchfile, FOPEN_READ_FLAGS)) == NULL) 
	{
		sprintf((char*)error, "\"%s\" %s", patchfile, strerror(errno));
		return -1;
	}	
	if (fseek(dpf, 32 + header.bzctrllen, SEEK_SET)) 
	{
		sprintf((char*)error, "\"%s\" %s", patchfile, strerror(errno));
		return -1;
	}	
	if ((dpfbz2 = BZ2_bzReadOpen(&dbz2err, dpf, 0, 0, NULL, 0)) == NULL) 
	{
		sprintf((char*)error, "BZ2_bzReadOpen, bz2err = %d", cbz2err);
		return -1;
	}	
	if ((epf = fopen(patchfile, FOPEN_READ_FLAGS)) == NULL) 
	{
		sprintf((char*)error, "\"%s\" %s", patchfile, strerror(errno));
		return -1;
	}		
	if (fseek(epf, 32 + header.bzctrllen + header.bzdatalen, SEEK_SET)) 
	{
		sprintf((char*)error, "\"%s\" %s", patchfile, strerror(errno));
		return -1;
	}	
	if ((epfbz2 = BZ2_bzReadOpen(&ebz2err, epf, 0, 0, NULL, 0)) == NULL) 
	{
		sprintf((char*)error, "BZ2_bzReadOpen, bz2err = %d", ebz2err);
		return -1;
	}

	if(((fd=open(oldfile,OPEN_FLAGS,0))<0) ||
		((oldsize=lseek(fd,0,SEEK_END))==-1) ||
		((old=malloc(oldsize+1))==NULL) ||
		(lseek(fd,0,SEEK_SET)!=0) ||
		(readFileToBuffer(fd,old,oldsize)!=oldsize) ||
		(close(fd)==-1)) {
		sprintf((char*)error, "\"%s\" %s", oldfile, strerror(errno));
		return -1;
	}
	if((new=malloc(header.newsize+1))==NULL) 
	{
		sprintf((char*)error, "%s", strerror(errno));
		return -1;
	} 

	oldpos=0;newpos=0;
	while(newpos<header.newsize) 
	{
		if(callback)
			callback(newpos, header.newsize, progressWorker);
		/* Read control data */
		for(i=0;i<=2;i++) 
		{
			lenread = BZ2_bzRead(&cbz2err, cpfbz2, buf, 8);
			if ((lenread < 8) || ((cbz2err != BZ_OK) && (cbz2err != BZ_STREAM_END))) 
			{
				sprintf((char*)error, "\"%s\"Corrupt patch", patchfile);
				return -1;
			}			
			ctrl[i]=offtin(buf);
		};

		/* Sanity-check */
		if(newpos+ctrl[0]>header.newsize) 
		{
			sprintf((char*)error, "\"%s\"Corrupt patch", patchfile);
			return -1;
		}			

		/* Read diff string */
		lenread = BZ2_bzRead(&dbz2err, dpfbz2, new + newpos, ctrl[0]);
		if ((lenread < ctrl[0]) || ((dbz2err != BZ_OK) && (dbz2err != BZ_STREAM_END))) 
		{
			sprintf((char*)error, "\"%s\"Corrupt patch", patchfile);
			return -1;
		}			

		/* Add old data to diff string */
		for(i=0;i<ctrl[0];i++)
			if((oldpos+i>=0) && (oldpos+i<oldsize))
				new[newpos+i]+=old[oldpos+i];

		/* Adjust pointers */
		newpos+=ctrl[0];
		oldpos+=ctrl[0];

		/* Sanity-check */
		if(newpos+ctrl[1] > header.newsize) 
		{
			sprintf((char*)error, "\"%s\"Corrupt patch", patchfile);
			return -1;
		}		

		/* Read extra string */
		lenread = BZ2_bzRead(&ebz2err, epfbz2, new + newpos, ctrl[1]);
		if ((lenread < ctrl[1]) || ((ebz2err != BZ_OK) && (ebz2err != BZ_STREAM_END))) 
		{
			sprintf((char*)error, "\"%s\"Corrupt patch", patchfile);
			return -1;
		}		

		/* Adjust pointers */
		newpos+=ctrl[1];
		oldpos+=ctrl[2];
	};

	/* Clean up the bzip2 reads */
	BZ2_bzReadClose(&cbz2err, cpfbz2);
	BZ2_bzReadClose(&dbz2err, dpfbz2);
	BZ2_bzReadClose(&ebz2err, epfbz2);
	if (fclose(cpf) || fclose(dpf) || fclose(epf)) 
	{
		sprintf((char*)error, "\"%s\" %s", patchfile, strerror(errno));
		return -1;
	}

	/* Write the new file */
	if(((fd=open(newfile,OPEN_FLAGS_CREATE,0666))<0) || 
		(writeFileFromBuffer(fd,new,header.newsize)!=header.newsize) || (close(fd)==-1)) 
	{
		sprintf((char*)error, "\"%s\" %s", newfile, strerror(errno));
		return -1;
	}	

	free(new);
	free(old);

	return 0;
}

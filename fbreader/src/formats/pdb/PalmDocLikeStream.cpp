/*
 * Copyright (C) 2004-2010 Geometer Plus <contact@geometerplus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <ZLFile.h>
#include <ZLResource.h>

#include "PalmDocLikeStream.h"


PalmDocLikeStream::PalmDocLikeStream(const ZLFile &file) : PdbStream(file) {
}

PalmDocLikeStream::~PalmDocLikeStream() {
	close();
}

bool PalmDocLikeStream::open() {
	myErrorCode = ERROR_NONE;
	if (!PdbStream::open()) {
		myErrorCode = ERROR_UNKNOWN;
		return false;
	}
	
	if (!processZeroRecord()) {
		return false;
	}

	myBuffer = new char[myMaxRecordSize];
	myRecordIndex = 0;
	return true;
}

bool PalmDocLikeStream::fillBuffer() {
	while (myBufferOffset == myBufferLength) {
		if (myRecordIndex + 1 > myMaxRecordIndex) {
			return false;
		}
		++myRecordIndex;
		if (!processRecord()) {
			return false;
		} 
	}
	//myBufferOffset = 0;
	return true;
}

const std::string &PalmDocLikeStream::error() const {
	static const ZLResource &resource = ZLResource::resource("mobipocketPlugin");
	switch (myErrorCode) {
		default:
		{
			static const std::string EMPTY;
			return EMPTY;
		}
		case ERROR_UNKNOWN:
			return resource["unknown"].value();
		case ERROR_COMPRESSION:
			return resource["unsupportedCompressionMethod"].value();
		case ERROR_ENCRYPTION:
			return resource["encryptedFile"].value();
	}
}

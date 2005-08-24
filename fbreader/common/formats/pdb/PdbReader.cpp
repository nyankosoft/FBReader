/*
 * FBReader -- electronic book reader
 * Copyright (C) 2004, 2005 Nikolay Pultsin <geometer@mawhrin.net>
 * Copyright (C) 2005 Mikhail Sobolev <mss@mawhrin.net>
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

#include <iostream>
#include <algorithm>
#include <vector>

#include <abstract/ZLZDecompressor.h>
#include <abstract/ZLStringUtil.h>
#include <abstract/ZLImage.h>
#include <abstract/ZLFSManager.h>

#include "PdbReader.h"
#include "../../bookmodel/BookModel.h"
#include "../../bookmodel/BookReader.h"

class ZLPluckerMultiImage : public ZLMultiImage {

public:
	ZLPluckerMultiImage(unsigned int rows, unsigned int columns, const ImageMap &imageMap) IMAGE_SECTION;
	~ZLPluckerMultiImage() IMAGE_SECTION;

	void addId(const std::string &id) IMAGE_SECTION;

	unsigned int rows() const;
	unsigned int columns() const;
	const ZLImage *subImage(unsigned int row, unsigned int column) const;

private:
	unsigned int myRows, myColumns;
	const ImageMap &myImageMap;
	std::vector<std::string> myIds;
};

inline ZLPluckerMultiImage::ZLPluckerMultiImage(unsigned int rows, unsigned int columns, const ImageMap &imageMap) : myRows(rows), myColumns(columns), myImageMap(imageMap) {}
inline ZLPluckerMultiImage::~ZLPluckerMultiImage() {}
inline void ZLPluckerMultiImage::addId(const std::string &id) { myIds.push_back(id); }
inline unsigned int ZLPluckerMultiImage::rows() const { return myRows; }
inline unsigned int ZLPluckerMultiImage::columns() const { return myColumns; }

const ZLImage *ZLPluckerMultiImage::subImage(unsigned int row, unsigned int column) const {
	unsigned int index = row * myColumns + column;
	if (index >= myIds.size()) {
		return 0;
	}
	ImageMap::const_iterator entry = myImageMap.find(myIds[index]);
	return (entry != myImageMap.end()) ? entry->second : 0;
}

static void readUnsignedShort(shared_ptr<ZLInputStream> stream, unsigned short &N) {
	stream->read((char*)&N + 1, 1);
	stream->read((char*)&N, 1);
}

static void readUnsignedLong(shared_ptr<ZLInputStream> stream, unsigned long &N) {
	stream->read((char*)&N + 3, 1);
	stream->read((char*)&N + 2, 1);
	stream->read((char*)&N + 1, 1);
	stream->read((char*)&N, 1);
}

struct PdbHeader {
	std::string DocName;
	unsigned short Flags;
	std::string Id;

	bool read(shared_ptr<ZLInputStream> stream) FORMATS_SECTION;
};

bool PdbHeader::read(shared_ptr<ZLInputStream> stream) {
	size_t startOffset = stream->offset();

	char docName[33];
	stream->read(docName, 32);
	docName[32] = '\0';
	DocName = docName;

	readUnsignedShort(stream, Flags);

	stream->seek(26);
	
	char id[9];
	stream->read(id, 8);
	id[8] = '\0';
	Id = id;
	stream->seek(4);
	return stream->offset() == startOffset + 72;
}

class PluckerReader : public BookReader {

public:
	PluckerReader(const std::string &filePath, shared_ptr<ZLInputStream> stream, BookModel &model);
	~PluckerReader();

	bool readDocument();

private:
	enum FontType {
		FT_REGULAR = 0,
		FT_H1 = 1,
		FT_H2 = 2,
		FT_H3 = 3,
		FT_H4 = 4,
		FT_H5 = 5,
		FT_H6 = 6,
		FT_BOLD = 7,
		FT_TT = 8,
		FT_SMALL = 9,
		FT_SUB = 10,
		FT_SUP = 11
	};

	void readRecord(size_t recordSize);
	void processCompressedTextRecord(size_t compressedSize, size_t uncompressedSize, const std::vector<int> &pars);
	void processTextParagraph(char *start, char *end);
	void processTextFunction(char *ptr);
	void setFont(FontType font, bool start);
	void changeFont(FontType font);

	void safeAddControl(TextKind kind, bool start);
	void safeAddHyperlinkControl(const std::string &id);
	void safeBeginParagraph();

private:
	std::string myFilePath;
	shared_ptr<ZLInputStream> myStream;
	FontType myFont;
	char *myBuffer;
	bool myParagraphStarted;
	std::vector<std::pair<TextKind,bool> > myDelayedControls;
	std::vector<std::string> myDelayedHyperlinks;
};

PluckerReader::PluckerReader(const std::string &filePath, shared_ptr<ZLInputStream> stream, BookModel &model) : BookReader(model), myFilePath(filePath), myStream(stream), myFont(FT_REGULAR) {
	myBuffer = new char[65535];
}

PluckerReader::~PluckerReader() {
	delete[] myBuffer;
}

void PluckerReader::safeAddControl(TextKind kind, bool start) {
	if (myParagraphStarted) {
		addControl(kind, start);
	} else {
		myDelayedControls.push_back(std::pair<TextKind,bool>(kind, start));
	}
}

void PluckerReader::safeAddHyperlinkControl(const std::string &id) {
	if (myParagraphStarted) {
		addHyperlinkControl(HYPERLINK, id);
	} else {
		myDelayedControls.push_back(std::pair<TextKind,bool>(HYPERLINK, true));
		myDelayedHyperlinks.push_back(id);
	}
}

void PluckerReader::safeBeginParagraph() {
	if (!myParagraphStarted) {
		int idIndex = 0;
		myParagraphStarted = true;
		beginParagraph();
		for (std::vector<std::pair<TextKind,bool> >::const_iterator it = myDelayedControls.begin(); it != myDelayedControls.end(); it++) {
			if ((it->first == HYPERLINK) && it->second) {
				addHyperlinkControl(HYPERLINK, myDelayedHyperlinks[idIndex]);
				idIndex++;
			} else {
				addControl(it->first, it->second);
			}
		}
		myDelayedControls.clear();
		myDelayedHyperlinks.clear();
	}
}

void PluckerReader::setFont(FontType font, bool start) {
	switch (font) {
		case FT_REGULAR:
			break;
		case FT_H1:
		case FT_H2:
		case FT_H3:
		case FT_H4:
		case FT_H5:
		case FT_H6:
			if (start) {
				enterTitle();
				pushKind(SECTION_TITLE);
			} else {
				popKind();
				exitTitle();
			}
			break;
		case FT_BOLD:
			safeAddControl(STRONG, start);
			break;
		case FT_TT:
			safeAddControl(CODE, start);
			break;
		case FT_SMALL:
			break;
		case FT_SUB:
			safeAddControl(SUB, start);
			break;
		case FT_SUP:
			safeAddControl(SUP, start);
			break;
	}
}

void PluckerReader::changeFont(FontType font) {
	if (myFont == font) {
		return;
	}
	setFont(myFont, false);
	myFont = font;
	setFont(myFont, true);
}

static void listParameters(char *ptr) {
	int argc = ((unsigned char)*ptr) % 8;
	//std::cerr << (int)(unsigned char)*ptr << "(";	
	for (int i = 0; i < argc - 1; i++) {
		ptr++;
		//std::cerr << (int)*ptr << ", ";	
	}
	if (argc > 0) {
		ptr++;
		//std::cerr << (int)*ptr;	
	}
	//std::cerr << ")\n";	
}

static unsigned int twoBytes(char *ptr) {
	return 256 * (unsigned char)*ptr + (unsigned char)*(ptr + 1);
}

static std::string fromNumber(unsigned int num) {
	std::string str;
	ZLStringUtil::appendNumber(str, num);
	return str;
}

void PluckerReader::processTextFunction(char *ptr) {
	switch ((unsigned char)*ptr) {
		case 0x08:
			safeAddControl(HYPERLINK, false);
			break;
		case 0x0A:
			safeAddHyperlinkControl(fromNumber(twoBytes(ptr + 1)));
			break;
		case 0x0C:
			safeAddHyperlinkControl(fromNumber(twoBytes(ptr + 1)) + "#" + fromNumber(twoBytes(ptr + 3)));
			break;
		case 0x11:
			changeFont((FontType)*(ptr + 1));
			break;
		case 0x1A:
			addImageReference(fromNumber(twoBytes(ptr + 1)));
			break;
		case 0x22: listParameters(ptr); break;
		case 0x29:
			switch (*(ptr + 1)) {
				case 0: safeAddControl(LEFT_ALIGNED, true); break;
				case 1: safeAddControl(RIGHT_ALIGNED, true); break;
				case 2: safeAddControl(CENTER_ALIGNED, true); break;
				case 3: safeAddControl(JUSTIFY_ALIGNED, true); break;
			}
			break;
		case 0x33: listParameters(ptr); break;
		case 0x38: listParameters(ptr); break;
		case 0x40: 
			safeAddControl(EMPHASIS, true);
			break;
		case 0x48:
			safeAddControl(EMPHASIS, false);
			break;
		case 0x53: // color setting is ignored
			break;
		case 0x5C:
			addImageReference(fromNumber(twoBytes(ptr + 3)));
			break;
		case 0x60: // underlined text is ignored
			break;
		case 0x68: // underlined text is ignored
			break;
		case 0x70: // strike-through text is ignored
			break;
		case 0x78: // strike-through text is ignored
			break;
		case 0x83: listParameters(ptr); break;
		case 0x85: listParameters(ptr); break;
		case 0x8E: listParameters(ptr); break;
		case 0x8C: listParameters(ptr); break;
		case 0x8A: listParameters(ptr); break;
		case 0x88: listParameters(ptr); break;
		case 0x90: // TODO: process table
			break;
		case 0x92: // TODO: process table
			break;
		case 0x97: // TODO: process table
			break;
		default: listParameters(ptr); break;
	}
}

void PluckerReader::processTextParagraph(char *start, char *end) {
	//std::cerr << "\n<PAR>\n";
	changeFont(FT_REGULAR);
	while (popKind()) {}
	//pushKind(REGULAR);

	myParagraphStarted = false;

	char *textStart = start;
	bool functionFlag = false;
	char *ptr = start;
	for (; ptr != end; ptr++) {
		if (*ptr == 0) {
			functionFlag = true;
			if (ptr != textStart) {
				safeBeginParagraph();
				addDataToBuffer(textStart, ptr - textStart);
				//std::string txt;
				//txt.append(textStart, ptr - textStart);
				//std::cerr << "text = " << txt << "\n";
			}
		} else if (functionFlag) {
			int paramCounter = ((unsigned char)*ptr) % 8;
			if (end - ptr > paramCounter + 1) {
				processTextFunction(ptr);
				ptr += paramCounter;
			} else {
				ptr = end - 1;
			}
			functionFlag = false;
			textStart = ptr + 1;
		} else if ((unsigned char)*ptr == 0xA0) {
			*ptr = 0x20;
		}
	}
	if (ptr != textStart) {
		safeBeginParagraph();
		addDataToBuffer(textStart, ptr - textStart);
		//std::string txt;
		//txt.append(textStart, ptr - textStart);
		//std::cerr << "text = " << txt << "\n";
	}
	if (myParagraphStarted) {
		endParagraph();
	}
}

void PluckerReader::processCompressedTextRecord(size_t compressedSize, size_t uncompressedSize, const std::vector<int> &pars) {
	//std::cerr << "\n<RECORD>\n";

	ZLZDecompressor decompressor(compressedSize);
	if (decompressor.decompress(*myStream, myBuffer, uncompressedSize) != uncompressedSize) {
		return;
	}

	char *start = myBuffer;
	char *end = myBuffer;

	for (std::vector<int>::const_iterator it = pars.begin(); it != pars.end(); it++) {
		start = end;
		end = start + *it;
		if (end > myBuffer + uncompressedSize) {
			return;
		}
		processTextParagraph(start, end);
	}

		/*
		if (functionFlag) {
			switch (*ptr) {
				case 0x22:
					// TODO: set margin
					ptr += 2;
					endParagraph();
					beginParagraph();
					processed = true;
					break;
				case 0x33:
					ptr += 3;
					endParagraph();
					beginParagraph(Paragraph::EMPTY_LINE_PARAGRAPH);
					endParagraph();
					beginParagraph();
					processed = true;
					break;
				case 0x38:
					endParagraph();
					beginParagraph();
					processed = true;
					break;
			}
			*/
}

void PluckerReader::readRecord(size_t recordSize) {
	unsigned short uid;
	readUnsignedShort(myStream, uid);
	if (uid == 1) {
		unsigned short version;
		readUnsignedShort(myStream, version);
	} else {
		unsigned short paragraphs;
		readUnsignedShort(myStream, paragraphs);

		unsigned short size;
		readUnsignedShort(myStream, size);

		unsigned char type;
		myStream->read((char*)&type, 1);

		unsigned char flags;
		myStream->read((char*)&flags, 1);

		switch (type) {
			case 1: // compressed text
				{
					std::vector<int> pars;
					for (int i = 0; i < paragraphs; i++) {
						unsigned short pSize;
						readUnsignedShort(myStream, pSize);
						pars.push_back(pSize);
						myStream->seek(2);
					}
					myStream->seek(2);
					std::string strId;
					ZLStringUtil::appendNumber(strId, uid);
					addHyperlinkLabel(strId);
					processCompressedTextRecord(recordSize - 10 - 4 * paragraphs, size, pars);
				}
				if ((flags & 0x1) == 0) {
					insertEndOfSectionParagraph();
				}
				break;
			case 3: // compressed image
				{
					myStream->seek(2);
					std::string strId;
					ZLStringUtil::appendNumber(strId, uid);
					addImage(strId, new ZLZCompressedFileImage("image/palm", myFilePath, myStream->offset(), recordSize - 10));
				}
				break;
			case 10:
				unsigned short typeCode;
				readUnsignedShort(myStream, typeCode);
				//std::cerr << "type = " << (int)type << "; ";
				//std::cerr << "typeCode = " << typeCode << "\n";
				break;
			case 15: // multiimage
			{
				//std::cerr << "uid = " << (int)uid << "; ";
				//std::cerr << "type = " << (int)type << "; ";
				unsigned short columns;
				unsigned short rows;
				::readUnsignedShort(myStream, columns);
				::readUnsignedShort(myStream, rows);
				ZLPluckerMultiImage *image = new ZLPluckerMultiImage(rows, columns, model().imageMap());
				for (int i = 0; i < size / 2 - 2; i++) {
					unsigned short us;
					::readUnsignedShort(myStream, us);
					std::string id;
					ZLStringUtil::appendNumber(id, us);
					image->addId(id);
					//std::cerr << us << " ";
				}
				//std::cerr << "\n";
				std::string strId;
				ZLStringUtil::appendNumber(strId, uid);
				addImage(strId, image);
				break;
			}
			default:
				//std::cerr << "type = " << (int)type << "\n";
				//std::cerr << "size = " << size << "\n";
				break;
		}
	}
}

bool PluckerReader::readDocument() {
	setMainTextModel();
	//pushKind(REGULAR);
	myFont = FT_REGULAR;

	std::vector<unsigned long> offsets;
	// record-id list
	myStream->seek(4);
	unsigned short numRecords;
	::readUnsignedShort(myStream, numRecords);
	offsets.reserve(numRecords);

	for (int i = 0; i < numRecords; i++) {
		unsigned long recordOffset;
		::readUnsignedLong(myStream, recordOffset);
		offsets.push_back(recordOffset);
		myStream->seek(4);
	}
	myStream->seek(2);
	for (std::vector<unsigned long>::const_iterator it = offsets.begin(); it != offsets.end(); it++) {
		size_t currentOffset = myStream->offset();
		if (currentOffset > *it) {
			break;
		}
		myStream->seek(*it - currentOffset);
		if (myStream->offset() != *it) {
			break;
		}
		//std::cerr << "currentOffset = " << myStream->offset() << "\n";
		size_t recordSize = ((it != offsets.end() - 1) ? *(it + 1) : myStream->sizeOfOpened()) - *it;
		readRecord(recordSize);
	}
	return true;
}

bool PdbReader::readDocument(const std::string &myFilePath, BookModel &model) {
	shared_ptr<ZLInputStream> stream = ZLFile(myFilePath).inputStream();
	if (stream.isNull() || !stream->open()) {
		return false;
	}

	PdbHeader header;
	if (!header.read(stream)) {
		stream->close();
		return false;
	}

	//std::cerr << "name = " << header.DocName << "\n";
	//std::cerr << "id = " << header.Id << "\n";

	bool code = false;
	if (header.Id == "DataPlkr") {
		code = PluckerReader(myFilePath, stream, model).readDocument();
	} else if (header.Id == "TEXtREAd") {
	}

	stream->close();
	return code;
}

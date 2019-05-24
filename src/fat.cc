#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <alloca.h>
#include "fat.hh"

unsigned FAT::File::tell() {

	unsigned offset = this->fat->offsetData;
	offset += ((this->cluster - 2) * this->fat->sizeCluster);
	offset += this->sector;

	return offset;

}

bool FAT::File::seek(unsigned bytes, bool relative) {

	// TODO: if !relative

	this->i += bytes;

	for (;;) {

		if (this->i >= fat->sizeSector) {

			this->sector++;
			this->i -= this->fat->sizeSector;

		}

		if (this->sector >= fat->sizeCluster) {

			int result = fat->getFATEntry(this->cluster);

			if (result == -1) return true; // TODO: in case of error?
			this->cluster = result;

			if (this->cluster >= 0x0FFFFFF8) return true;

			this->sector -= this->fat->sizeCluster;

		}

		break;

	}

	return false;

}

FAT::File::File() {}

FAT::File::File(FAT *fat): fat(fat) {}

FAT::Result FAT::File::bind(const char *path, File *_base) {

	this->reset();

	// TODO: use malloc?
	char *real = (char*) alloca(strlen(path) + 1);
	strcpy(real, path);

	File base;

	if (_base) base = *_base;
	else {

		// dummy for root directory

		strcpy(base.name, "/");
		base.attr = FAT::ATTR_SYS | FAT::ATTR_DIR;
		base.baseCluster = this->fat->rootCluster;
		base.size = 0;

		base.reset(this->fat);

	}

	for (;;) {

		if (!*real) {

			*this = base;
			return FAT::INF_SUCCESS;

		}

		if (*real == '/') {

			real++;
			continue;

		}

		char *next = strchr(real, '/');
		if (next) {

			*next = '\0';
			next++;

		}

		File file;

		for (;;) {

			FAT::Result result = base.readEntry(&file);
			if (result == FAT::ERR_READ) continue;
			if (result == FAT::INF_STOP) return FAT::ERR_READ;

			if (!strcasecmp(file.name, real)) {

				if (next) break;

				*this = file;
				return FAT::INF_SUCCESS;

			}

			if (result == FAT::INF_LAST) return FAT::ERR_READ;

		}

		real = next;
		base = file;

	}

}


void FAT::File::reset(FAT *fat) {

	if (fat) this->fat = fat;

	this->cluster = this->baseCluster;
	this->sector = 0;
	this->i = 0;

}


FAT::Result FAT::File::readEntry(File *file) {

	// special case for /dir/.. of root dir
	if (!this->baseCluster) {

		this->baseCluster = this->fat->rootCluster;
		this->reset();

	}

	// TODO: use sizeSector
	uint8_t buf[512];

	// loop until found valid entry or end of chain
	for (;;) {

		// TODO: cache sector in pRead
		bool result = this->fat->readSector(buf, this->tell());
		if (!result) return FAT::ERR_READ;

		switch (buf[this->i]) {

			case 0x00: // free
			case 0xE5: // deleted

				// if end of chain, stop
				bool done = this->seek(this->fat->sizeEntry);
				if (done) return FAT::INF_STOP;

				continue;

		}

		// convert FAT32 8.3 file name to a normal one
		FAT::nameFrom83((char*) buf + this->i, file->name);

		file->attr    = memGet(buf, FAT::BYTE,  this->i + 0x0B);

		unsigned high = memGet(buf, FAT::WORD,  this->i + 0x14);
		unsigned low  = memGet(buf, FAT::WORD,  this->i + 0x1A);
		file->baseCluster = (high << 16) | low;

		file->size    = memGet(buf, FAT::DWORD, this->i + 0x1C);
		file->reset(fat);

		break;

	}

	// check for end of chain
	bool done = this->seek(this->fat->sizeEntry);
	return done ? FAT::INF_LAST : FAT::INF_SUCCESS;

}

FAT::Result FAT::File::read(void *dest, unsigned bytes) {

	uint8_t buf[512];

	for (;;) {

		bool result;

		result  = this->fat->readSector(buf, this->tell());
		if (!result) return FAT::ERR_READ;

		unsigned avail = 512 - this->i;
		if (bytes < avail) avail = bytes;

		memcpy(dest, buf, avail);

		*((uint8_t**) &dest) += avail;
		bytes -= avail;

		if (!bytes) break;

		// if more bytes left and reached end of chain then error

		result = this->seek(avail);
		if (result) return FAT::ERR_READ;

	}

	return FAT::INF_SUCCESS;

}

/*bool FAT::File::mknod(const char *path, Attr attr, File *base) {

	uint8_t buf[512];

	for (;;) {

		bool result;

		result = this->fat->readSector(buf, this->tell());
		if (!result) return false;

		for (unsigned i = 0; i < fat->sizeSector; i += fat->sizeEntry) {

			switch (buf[i]) {

				case 0x00: // free
				case 0xE5: // deleted

				// TODO:
				return true;

				default: continue;

			}

		}

		result = this->seek(512);

	}

	return true;

}*/

////////////////////////////////////////////////////////////////////////

FAT::FAT(const char *path) {

	fd = ::open(path, O_RDWR);
	if (fd == -1) {

		fprintf(stderr, "Error opening file '%s': %s\n", path, strerror(errno));
		return;

	}

	uint8_t buf[512];

	bool result;

	result = FAT::readSector(buf, 0);
	if (!result) {

		printf("Error loading BIOS parameter block\n");
		return;

	}

	if (memGet(buf, FAT::WORD, 0x1FE) != 0xAA55) {

		printf("Error: Invalid boot signature\n");
		return;

	}

	if (memGet(buf, FAT::BYTE, 0x042) != 0x29) {

		printf("Was 0x%X", memGet(buf, FAT::BYTE, 0x042));

		printf("Error: Invalid extended boot signature (should be 0x29)\n");
		return;

	}

	strncpy(oemIdentifier, (char*) buf + 0x003, 0x8);

	if (memGet(buf, FAT::WORD, 0x00B) != 512) {

		printf("Error: Invalid value for 'Bytes Per Sector'\n");
		return;

	}

	sizeCluster = memGet(buf, FAT::BYTE, 0x00D);
	switch (sizeCluster) {

		case 1:
		case 2:
		case 4:
		case 8:
		case 16:
		case 32:
		case 64:
		case 128: break;
		default:

			printf("Error: Invalid value for 'Sectors Per Cluster'");
			return;

	}

	reservedSectors = memGet(buf, FAT::WORD, 0x00E);

	numFATS = memGet(buf, FAT::BYTE, 0x010);
	if (!numFATS) {

		// TODO: is this correct?

		printf("Error: Invalid number of FATs\n");
		return;

	}

	unsigned numDirEntries = memGet(buf, FAT::WORD, 0x011);
	if (numDirEntries) {

		printf("Error: Invalid max root directory entries value\n");
		return;

	}

	// 0x020 is for sectors above 65535

	numSectors = memGet(buf, FAT::WORD, 0x013);
	if (!numSectors) numSectors = memGet(buf, FAT::DWORD, 0x020);
	if (!numSectors) {

		// TODO: use offset 0x052 for 64 bit number
		// probably won't have to though

		printf("Error: Invalid value for 'Number of Sectors'\n");
		return;

	}

	rootCluster = memGet(buf, FAT::DWORD, 0x02C);
	if (!rootCluster) {

		printf("Error: Invalid value for 'Root Cluster'\n");
		return;

	}

	mediaDescriptorType = memGet(buf, FAT::BYTE, 0x015);
	// TODO: check if matches the descriptor in every FAT

	sectorsPerTrack = memGet(buf, FAT::WORD, 0x018);
	numHeads = memGet(buf, FAT::WORD, 0x01A);
	numHiddenSectors = memGet(buf, FAT::DWORD, 0x01C);

	sectorsPerFAT = memGet(buf, FAT::DWORD, 0x024);
	if (!sectorsPerFAT) {

		printf("Error: Invalid number of sectors per FAT\n");
		return;

	}

	unsigned flags = memGet(buf, FAT::WORD, 0x028);

	mirroring = !(flags & (1 << 7));
	activeFAT = flags & 0xF;

	fatVersion = memGet(buf, FAT::WORD, 0x02A);
	if (fatVersion) {

		printf("Error: Invalid FAT version (should be v0.0)\n");
		return;

	}

	rootCluster = memGet(buf, FAT::DWORD, 0x02C);
	if (!rootCluster) {

		printf("Error: Invalid root cluster value\n");
		return;

	}

	infoSector = memGet(buf, FAT::WORD, 0x030);
	if (!infoSector) {

		printf("Error: Invalid value for 'Information Sector'\n");
		return;

	}

	backupBootSector = memGet(buf, FAT::WORD, 0x032);
	driveNumber = memGet(buf, FAT::BYTE, 0x040);
	dirty = memGet(buf, FAT::BYTE, 0x41) & 0x1;

	serialNumber = memGet(buf, FAT::DWORD, 0x043);

	strncpy(volumeLabel, (char*) buf + 0x047, 11);
	strncpy(systemIdentifier, (char*) buf + 0x052, 8);

	result = FAT::readSector(buf, infoSector);
	if (!result) {

		printf("Error: Error loading FS information sector\n");
		return;

	}

	if (memGet(buf, FAT::DWORD, 0x000) != 0x41615252) {

		printf("Error: Invalid FS information signature\n");
		return;

	}

	if (memGet(buf, FAT::DWORD, 0x1E4) != 0x61417272) {

		printf("Error: Invalid FS information signature\n");
		return;

	}

	if (memGet(buf, FAT::DWORD, 0x1FC) != 0xAA550000) {

		printf("Error: Invalid FS information signature\n");
		return;

	}

	freeClusters = memGet(buf, FAT::DWORD, 0x1E8);
	lastCluster = memGet(buf, FAT::DWORD, 0x1EC);

	offsetData = reservedSectors + (numFATS * sectorsPerFAT);
	this->error = false;

}

FAT::~FAT() {

	// TODO: save freeSectors and lastSector
	close(fd);

}

bool FAT::operator!() {

	// TODO: do fsck?
	return error;

}

void FAT::debugInfo() {

	printf("OEM identifier        : \"%s\"\n", oemIdentifier);
	printf("Sectors per cluster   : %i\n", sizeCluster);
	printf("Reserved sectors      : %i\n", reservedSectors);
	printf("FATs                  : %i\n", numFATS);
	printf("Sectors               : %i\n", numSectors);
	printf("Media descriptor type : 0x%X\n", mediaDescriptorType);
	printf("Sectors per track     : %i\n", sectorsPerTrack);
	printf("Heads                 : %i\n", numHeads);
	printf("Hidden sectors        : %i\n", numHiddenSectors);
	printf("Sectors per FAT       : %i\n", sectorsPerFAT);
	printf("Mirroring             : %s\n", mirroring ? "True" : "False");
	printf("Active FAT            : %i\n", activeFAT);
	printf("Dirty                 : %s\n", dirty ? "True" : "False");
	printf("FAT version           : 0x%.4X\n", fatVersion);
	printf("Root cluster          : 0x%X\n", rootCluster);
	printf("Info sector           : 0x%X\n", infoSector);
	printf("Backup boot sector    : 0x%X\n", backupBootSector);
	printf("Drive number          : 0x%X\n", driveNumber);
	printf("Serial number         : 0x%X\n", serialNumber);
	printf("Volume label          : \"%s\"\n", volumeLabel);
	printf("System identifier     : \"%s\"\n", systemIdentifier);
	printf("Free clusters         : %i\n", freeClusters);
	printf("Last cluster          : 0x%X\n", lastCluster);

}

bool FAT::readSector(void *dest, unsigned start) {

	ssize_t result = pread(fd, dest, this->sizeSector, start * sizeSector);
	if (result == -1) {

		perror("Error reading sector: ");
		return false;

	}

	return true;

}

bool FAT::writeSector(void *src, unsigned start) {

	ssize_t result = pwrite(fd, src, this->sizeSector, start * sizeSector);
	if (result == -1) {

		perror("Error writing sector: ");
		return false;

	}

	return true;

}

unsigned FAT::memGet(void *buf, Size size, unsigned offset) {

	// TODO: change long?

	switch (size) {

		case FAT::BYTE:  return *((uint8_t*)  ((long) buf + offset));
		case FAT::WORD:  return *((uint16_t*) ((long) buf + offset));
		case FAT::DWORD: return *((uint32_t*) ((long) buf + offset));
		default: return 0;

	}

}

void FAT::memSet(void *buf, unsigned val, Size size, unsigned offset) {

	switch (size) {

		case FAT::BYTE:  *((uint8_t*)  ((long) buf + offset)) = val;
		break;

		case FAT::WORD:  *((uint16_t*) ((long) buf + offset)) = val;
		break;

		case FAT::DWORD: *((uint32_t*) ((long) buf + offset)) = val;

	}

}

void FAT::nameFrom83(char *in, char *out) {

	unsigned index1 = 7;
	// index of last non-space char

	for (unsigned i = 0; i < 8; i++) {

		if (in[i] != ' ') index1 = i;
		out[i] = tolower(in[i]);

	}

	index1 += 1;

	unsigned index2 = 0;

	for (unsigned i = 8; i < 11; i++)
		if (in[i] != ' ') out[index1 + 1 + index2++] = tolower(in[i]);

	if (index2) {

		out[index1] = '.';
		out[index1 + index2 + 1] = '\0';

	} else out[index1] = '\0';

}

int FAT::getFATEntry(unsigned n) {

	// TODO: use activeFAT and mirroring
	// TODO: why 128?!

	unsigned offset = this->reservedSectors;
	offset += n / 128;

	uint8_t buf[512];
	bool result = this->readSector(buf, offset);
	if (!result) return -1;

	// top 4 bits are reserved

	return memGet(buf, FAT::DWORD, (n % 128) * 4) & 0x0FFFFFFF;

}

bool FAT::setFATEntry(unsigned n, unsigned val) {

	unsigned sector = this->reservedSectors;
	sector += n / 128;

	unsigned offset = (n % 128) * FAT::DWORD;

	uint8_t buf[512];
	bool result = this->readSector(buf, sector);
	if (!result) return false;

	unsigned original = FAT::memGet(buf, FAT::DWORD, offset);

	FAT::memSet(buf, (original & 0xF0000000) | val, FAT::DWORD, offset);
	return this->writeSector(buf, sector);

}

int FAT::getFreeCluster() {

	unsigned last = ((this->numSectors - this->reservedSectors - (this->numFATS * this->sectorsPerFAT)) / 8) - 1;

	if (this->lastCluster == 0xFFFFFFFF) this->lastCluster = this->rootCluster;
	else if (this->lastCluster < 2 || this->lastCluster > last) {

		for (unsigned i = 0; i < sectorsPerFAT * 128; i++) {

			int entry = getFATEntry(i);
			if (entry == -1) return -1; // for error

			if (!entry) return i;

		}

		// read error or out of space
		return -1;

	}

	return this->lastCluster + 1;

}

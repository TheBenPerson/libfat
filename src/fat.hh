#ifndef LIBFAT_FAT
#define LIBFAT_FAT

struct FAT {

	enum Attr {

		ATTR_RO =         (0x1 << 0x0),
		ATTR_HIDDEN =     (0x1 << 0x1),
		ATTR_SYS =        (0x1 << 0x2),
		ATTR_VOLUME_ID =  (0x1 << 0x3),
		ATTR_DIR =        (0x1 << 0x4),
		ATTR_ARCHIVE =    (0x1 << 0x5),
		ATTR_RESERVED_1 = (0x1 << 0x6),
		ATTR_RESERVED_2 = (0x1 << 0x7)

	};

	enum Result {

		INF_SUCCESS,
		INF_STOP,
		INF_LAST,
		ERR_READ,
		ERR_NOT_DIR

	};

////////////////////////////////////////////////////////////////////////

	struct File {

		FAT *fat;

		// file's directory entry

		char name[13];
		unsigned attr;
		unsigned baseCluster;
		unsigned size;

		// read/write pointer

		unsigned cluster;
		unsigned sector;
		unsigned i;

		File();
		File(FAT *fat);

		// read/write pointer operations

		// @return: current LBA
		unsigned tell();

		// @return: true if reached end of chain
		bool seek(unsigned bytes, bool relative = true);

		void reset(FAT *fat = nullptr);

		// set members

		Result bind(const char *path, File *_base = nullptr);

		// read operations

		Result read(void *dest, unsigned bytes);

		// @return: next file entry in directory
		Result readEntry(File *file);

		// write operations

		bool writeEntry(const char *path, Attr attr);

	};

////////////////////////////////////////////////////////////////////////

	int fd;

	char oemIdentifier[9] = {0};
	unsigned sizeCluster;
	unsigned reservedSectors;
	unsigned numFATS;
	unsigned numSectors;
	unsigned mediaDescriptorType;
	unsigned sectorsPerTrack;
	unsigned numHeads;
	unsigned numHiddenSectors;
	unsigned sectorsPerFAT;
	bool mirroring;
	unsigned activeFAT;
	unsigned fatVersion;
	unsigned rootCluster;
	unsigned infoSector;
	unsigned backupBootSector;
	unsigned driveNumber;
	bool dirty;
	unsigned serialNumber;
	char volumeLabel[12] = {0};
	char systemIdentifier[9] = {0};

	unsigned freeClusters;
	unsigned lastCluster;

	unsigned sizeEntry = 32;
	unsigned sizeSector = 512;
	unsigned offsetData;

	bool error = true;

////////////////////////////////////////////////////////////////////////

	FAT(const char *path);
	~FAT();

	bool operator!();

	void debugInfo();

////////////////////////////////////////////////////////////////////////

	enum Size {

		BYTE  = 1,
		WORD  = 2,
		DWORD = 4

	};

	static unsigned memGet(void *buf, Size size, unsigned offset);
	static void memSet(void *buf, unsigned val, Size size, unsigned offset);

	// convert internal FAT names to 8.3 ones
	// eg "123     456" -> 123.456
	static void nameFrom83(char *in, char *out);

////////////////////////////////////////////////////////////////////////

	// replace with your own implementation
	bool readSector(void *dest, unsigned offset);
	bool writeSector(void *src, unsigned offset);

	// @returns: -1 in error
	int getFATEntry(unsigned n);
	bool setFATEntry(unsigned n, unsigned val);

	int getFreeCluster();

};

#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "fat.hh"

static const char *usage =
"Usage: fattool [filesystem] [command] [args]\n"
	"\tPerform operations on a FAT32 filesystem\n\n"

"Commands:\n"
"\tinf:            show FS info\n"
"\tls [directory]: list files\n"
"\tcat [file]:     concatenate files\n"
"\tdf:             show FS usage";

static void inf(FAT *fat);
static void ls(FAT *fat, char *path);
static void cat(FAT *fat, const char *path);
static void df(FAT *fat);
static void touch(FAT *fat, const char *path);

int main(int argc, char* argv[]) {

	if (argc < 3) {

		puts(usage);
		return EX_USAGE;

	}

	FAT fat(argv[1]);
	if (!fat) return 1;

	if (!strcmp(argv[2], "inf")) inf(&fat);
	if (!strcmp(argv[2], "ls")) ls(&fat, argv[3]);
	if (!strcmp(argv[2], "cat")) cat(&fat, argv[3]);
	if (!strcmp(argv[2], "df")) df(&fat);
	if (!strcmp(argv[2], "touch")) touch(&fat, argv[3]);

	puts(usage);
	return EX_USAGE;

}

static unsigned toHuman(unsigned num, const char **prefix) {

	const char* prefixes[] = { "B  ", "KiB", "MiB", "GiB" };

	unsigned i = 0;
	for (; num > 1024; i++)
		num /= 1024;

	*prefix = prefixes[i];
	return num;

}

void inf(FAT *fat) {

	fat->debugInfo();
	exit(EXIT_SUCCESS);

}

void ls(FAT *fat, char *path) {

	char base[] = "/";
	if (!path) path = base;

	FAT::File root(fat);
	FAT::Result result = root.bind(path);

	if (result != FAT::INF_SUCCESS) {

		printf("Error: '%s' not found\n", path);
		exit(EXIT_FAILURE);

	}

	if (!(root.attr & FAT::ATTR_DIR)) {

		printf("Error: '%s' is not a directory\n", path);
		exit(EXIT_FAILURE);

	}

	char *last = (char*) strrchr(path, '/');
	if (last && last[1] == '\0') *last = '\0';

	FAT::File file(fat);

	for (;;) {

		FAT::Result result = root.readEntry(&file);
		if (result == FAT::ERR_READ) continue;
		if (result == FAT::INF_STOP) break;

		if (file.attr & FAT::ATTR_VOLUME_ID) continue;

		char attr[] =  "----";

		if (file.attr & FAT::ATTR_DIR) attr[0] = 'd';
		else if (file.attr & FAT::ATTR_ARCHIVE) attr[0] = 'a';

		if (file.attr & FAT::ATTR_RO) attr[1] = 'r';
		if (file.attr & FAT::ATTR_HIDDEN) attr[2] = 'h';
		if (file.attr & FAT::ATTR_SYS) attr[3] = 's';

		const char *prefix;
		file.size = toHuman(file.size, &prefix);

		printf("%s %-4i %-3s %s/%s%s\n", attr, file.size, prefix, path, file.name, (file.attr & FAT::ATTR_DIR) ? "/" : "");
		if (result == FAT::INF_LAST) break;

	}

	exit(EXIT_SUCCESS);

}

void cat(FAT *fat, const char *path) {

	if (!path) {

		puts(usage);
		exit(EX_USAGE);

	}

	FAT::File file(fat);
	FAT::Result result = file.bind(path);

	if (result != FAT::INF_SUCCESS) {

		printf("Error: '%s' not found\n", path);
		exit(EXIT_FAILURE);

	}

	if (file.attr & FAT::ATTR_DIR) {

		printf("Error: '%s' is not a file\n", path);
		exit(EXIT_FAILURE);

	}

	char *buf = new char[file.size + 1];
	file.read(buf, file.size);
	write(STDOUT_FILENO, buf, file.size);

	delete[] buf;
	exit(EXIT_SUCCESS);

}

void df(FAT *fat) {

	// used/total remaining

	unsigned used = (fat->lastCluster - 2) * fat->sizeCluster * fat->sizeSector;
	unsigned total = (fat->numSectors - (fat->reservedSectors + (fat->numFATS * fat->sectorsPerFAT))) * fat->sizeSector;
	unsigned remaining = fat->freeClusters * fat->sizeCluster * fat->sizeSector;

	const char *prefix1;
	const char *prefix2;
	const char *prefix3;

	used = toHuman(used, &prefix1);
	total = toHuman(total, &prefix2);
	remaining = toHuman(remaining, &prefix3);

	printf("%i %s / %i %s: %i %s free\n", used, prefix1, total, prefix2, remaining, prefix3);
	exit(EXIT_SUCCESS);

}

void touch(FAT *fat, const char *path) {

	fat->mknod(path, FAT::ATTR_ARCHIVE);
	exit(EXIT_SUCCESS);

}

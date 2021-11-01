#include <assert.h>
#include <getopt.h>
#include <locale.h>
#include <monetary.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * header -> player_count
 * player1 -> next_ship_addr (0x12345678)
 * player1_ship1 -> next_ship_addr (0x12345679)
 * player1_shipN -> next_ship_addr (NULL)
 * ...
 * playerN
 * playerN_ship1
 * playerN_shipN
 * footer
 *
 * Parsing works something like this;
 * read the header, get the player_count
 * read 1st player, get next_ship_addr
 * read player ships while next_ship_addr
 * read 2nd player, get next_ship_addr
 * ...
 * read footer
*/


struct savegame {
	struct header {
		char sig[32];
		uint8_t filler0[6];
		uint8_t player_count;
		uint8_t filler1[32];
	} __attribute__ ((packed)) header;

	struct player {
		int32_t flag1;
		int32_t flag2;
		char name1[12];
		char name2[12];
		char company[12];
		uint8_t location;
		uint8_t filler0[3];
		int32_t money;
		uint8_t filler1[8];
		int16_t expenses;
		uint8_t filler2[26];
		uint32_t next_ship_addr;
		uint8_t filler3[4];
		int32_t money_table[256];
		uint8_t filler4[4];
	} __attribute__ ((packed)) *player;

	struct linked_list {
		struct linked_list *next;
		struct ship {
			uint32_t next_ship_addr;
			char name[10];
			uint8_t filler1[114];
		} __attribute__ ((packed)) ship;

	} **player_ships;

	uint8_t footer[12];
} __attribute__ ((packed));

void print_filler(const char* topic, uint8_t *src, size_t size);

int main(int argc, char *argv[]) {
	assert(sizeof (struct savegame::header) == 71);
	assert(sizeof (struct savegame::player) == 0x464);
	assert(sizeof (struct savegame::linked_list::ship) == 128);

	char *r = setlocale(LC_ALL, "nb_NO.UTF-8");
	if (r == NULL)
		fprintf(stderr, "Unable to set locale\n");
	else
		printf("setlocale: %s\n", r);

	int c, optindex = 0;

	static struct option long_options[] = {
		{ NULL, no_argument, NULL, 0 }
	};

	while ((c = getopt_long(argc, argv, "", long_options, &optindex)) != -1) {
		switch (c) {
			default:
				printf("unhandled case '%c' (0x%02x): ", c, c);
		}
	}

	if (optind >= argc) {
		exit(EXIT_FAILURE);
	}

	for (int fi = optind; fi < argc; fi++) {
		struct savegame sg;

		FILE *fp = fopen(argv[fi], "r");

		if (fp == NULL) {
			printf("Could not open file: %s\n", argv[fi]);
			exit(EXIT_FAILURE);
		}

		size_t res = 0;

		res = fread(&sg.header, sizeof (struct savegame::header), 1, fp);
		printf("Sig:     |%-32.32s|\n", sg.header.sig);

		print_filler("header filler0", sg.header.filler0, sizeof (sg.header.filler0));

		printf("Players: %d\n", sg.header.player_count);

		print_filler("header filler1", sg.header.filler1, sizeof (sg.header.filler1));

		sg.player = (struct savegame::player *) malloc(sizeof (struct savegame::player) * sg.header.player_count);
		sg.player_ships = (struct savegame::linked_list **) malloc(sizeof (struct savegame::linked_list *) * sg.header.player_count);



		char buf[1024];
		for (int p = 0; p < sg.header.player_count; p++) {
			res = fread(&sg.player[p], sizeof (struct savegame::player), 1, fp);
			struct savegame::player *cur = &sg.player[p];

			printf("Flag1: %d, Flag2: %d\n", sg.player[p].flag1, sg.player[p].flag2);

			printf("Name1:   |%-12s|\n", sg.player[p].name1);
			printf("Name2:   |%-12s|\n", sg.player[p].name2);
			printf("Company: |%-12s|\n", sg.player[p].company);

			printf("location: %02x\n", sg.player[p].location);
			/*
				0x19 alexandria
				0x13 basrah
				0x11 Buenos aries
				0x1b calcutta
			*/
			print_filler("player filler0", sg.player[p].filler0, sizeof (sg.player[p].filler0));

			res = strfmon(buf, sizeof (buf), "%!#0.0i", (double) sg.player[p].money * 1000);
			printf("Money: %s\n", buf);


			print_filler("player filler1", sg.player[p].filler1, sizeof (sg.player[p].filler1));
			printf("expenses: %d\n", sg.player[p].expenses);
			print_filler("player filler2", sg.player[p].filler2, sizeof (sg.player[p].filler2));

			printf("next_ship_addr: 0x%08x\n", sg.player[p].next_ship_addr);

			print_filler("player filler3", sg.player[p].filler3, sizeof (sg.player[p].filler3));
			/*
			size_t sizeof_table = sizeof (sg.player[p].money_table) / sizeof (*sg.player[p].money_table);
			printf("sizeof_table: %d\n", sizeof_table);

			for (int i = 0; i < sizeof_table; i++) {
				res = strfmon(buf, sizeof (buf), "%!#0.0i", (double) sg.player[p].money_table[i] * 1000);
				printf("[%3d]: %s\n", i, buf);
			}
			*/

			print_filler("player filler4", sg.player[p].filler4, sizeof (sg.player[p].filler4));

			/* I was looking for a ship count so you could just read 'em all in one go, but
			 * didn't find it. I did however find this one thing, the next_ship_addr was
			 * probably just an address to a linked list of ships, and an entry of NULL at
			 * the end of the list. Which you could reuse if you're * on 32-bit systems.
			 *
			 * This awful mess is my work-around for 64-bit.
			 * We need an additional place to store the next pointer, if next is not null,
			 * assume there are more ships to read out.
			 */

			uint32_t has_ship = sg.player[p].next_ship_addr;
			struct savegame::linked_list **head = &sg.player_ships[p];
			while (has_ship) {
				*head = (struct savegame::linked_list *) malloc(sizeof (struct savegame::linked_list));
				struct savegame::linked_list *item = *head;
				item->next = NULL;

				res = fread(&item->ship, sizeof (struct savegame::linked_list::ship), 1, fp);
				printf("next_ship_addr: 0x%08x\n", item->ship.next_ship_addr);
				printf("ship name: %10s\n", item->ship.name);
				print_filler("ship filler1", item->ship.filler1, sizeof (item->ship.filler1));

				has_ship = item->ship.next_ship_addr;
				head = &(item->next);
			}
		}

		res = fread(&sg.footer, sizeof (sg.footer), 1, fp);
		print_filler("footer", sg.footer, sizeof (sg.footer));

		size_t total = ftell(fp);
		printf("total bytes read: %d\n", total);
	}

	return EXIT_SUCCESS;
}

void print_filler(const char* topic, uint8_t *src, size_t size) {
	printf("%s:", topic);
	for (int i = 0; i < size; i++) {
		if (i % 4 == 0)
			printf("\n[%2d]: ", i);
		printf("%02x ", src[i]);
	}
	printf("\n");
}

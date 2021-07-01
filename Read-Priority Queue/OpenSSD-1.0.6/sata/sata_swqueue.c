#include "jasmine.h"
#include "sata.h"

typedef struct
{
	UINT32	lba;
	UINT32	sector_count;
	UINT32	cmd_type;
	UINT32	ref;
} CMD_R;

/* sw queue */
#define SIZE 128
CMD_R rqueue[SIZE];
CMD_R wqueue[SIZE];
CMD_T oqueue[SIZE * 2];
UINT32 WHEAD = 0;
UINT32 RHEAD = 0;
UINT32 WTAIL = 0;
UINT32 RTAIL = 0;
UINT32 HEAD = 0;
UINT32 TAIL = 0;

/* FIFO queue */
// DEQUEUE
void sw_eventq_pop_FIFO(CMD_T* cmd)
{
	while ((GETREG(SATA_EQ_DATA_2) & 8) != 0);

	cmd->lba = oqueue[HEAD].lba;
	cmd->sector_count = oqueue[HEAD].sector_count;
	cmd->cmd_type = oqueue[HEAD].cmd_type;
	HEAD = (HEAD + 1) % (SIZE * 2);

	if (cmd->sector_count == 0)
		cmd->sector_count = 0x10000;
}

 void sw_eventq_pop_RP(CMD_T* cmd)
{
	while ((GETREG(SATA_EQ_DATA_2) & 8) != 0);

	if (RHEAD != RTAIL){
		//if (HEAD == rqueue[RHEAD].ref){
			cmd->lba = rqueue[RHEAD].lba;
			cmd->sector_count = rqueue[RHEAD].sector_count;
			cmd->cmd_type = rqueue[RHEAD].cmd_type;
			HEAD = (HEAD + 1) % (SIZE * 2);
			RHEAD = (RHEAD + 1) % SIZE;
		//}
		/*else
		{
			UINT32 raw = 0;
			UINT32 rlba = rqueue[RHEAD].lba;
			UINT32 i = HEAD;
			UINT32 j = WHEAD;
			
			while (i != rqueue[RHEAD].ref){
				if (wqueue[j].ref != i){
					i = (i + 1) % (SIZE * 2);
				}
				else 
				{
					if (wqueue[j].lba == rlba)
					{
						raw = 1;
						break;
					}
					else {
						j = (j + 1) % SIZE;
						i = (i + 1) % (SIZE * 2);
					}
				}
			}
			if (raw == 0) {
				cmd->lba = rqueue[RHEAD].lba;
				cmd->sector_count = rqueue[RHEAD].sector_count;
				cmd->cmd_type = rqueue[RHEAD].cmd_type;
				HEAD = (HEAD + 1) % (SIZE * 2);
				RHEAD = (RHEAD + 1) % SIZE;
			}
			else {
				cmd->lba = wqueue[WHEAD].lba;
				cmd->sector_count = wqueue[WHEAD].sector_count;
				cmd->cmd_type = wqueue[WHEAD].cmd_type;
				HEAD = (HEAD + 1) % (SIZE * 2);
				WHEAD = (WHEAD + 1) % SIZE;
			}
		}*/
	}
	else
	{
		cmd->lba = wqueue[WHEAD].lba;
		cmd->sector_count = wqueue[WHEAD].sector_count;
		cmd->cmd_type = wqueue[WHEAD].cmd_type;
		HEAD = (HEAD + 1) % (SIZE * 2);
		WHEAD = (WHEAD + 1) % SIZE;
	}

	if (cmd->sector_count == 0)
		cmd->sector_count = 0x10000;
}

// ENQUEUE
 void sw_eventq_push(void)
{
	
	UINT32 lba, sector_count, cmd_code, cmd_type, fis_d1, fis_d3;

	cmd_code = (GETREG(SATA_FIS_H2D_0) & 0x00FF0000) >> 16;
	cmd_type = ata_cmd_class_table[cmd_code];
	fis_d1 = GETREG(SATA_FIS_H2D_1);
	fis_d3 = GETREG(SATA_FIS_H2D_3);

	if (!(cmd_type & (CCL_FTL_H2D | CCL_FTL_D2H)))
		return;
	
	if (cmd_type & ATR_LBA_NOR)
	{
		if ((fis_d1 & BIT30) == 0)	// CHS
		{
			UINT32 cylinder = (fis_d1 & 0x00FFFF00) >> 8;
			UINT32 head = (fis_d1 & 0x0F000000) >> 24;  
			UINT32 sector = fis_d1 & 0x000000FF;

			lba = (cylinder * g_sata_context.chs_cur_heads + head) * g_sata_context.chs_cur_sectors + sector - 1;
		}
		else
		{
			lba = fis_d1 & 0x0FFFFFFF;
		}

		sector_count = fis_d3 & 0x000000FF;

		if (sector_count == 0 && (cmd_type & ATR_NO_SECT) == 0)
		{
			sector_count = 0x100;
		}
	}
	else if (cmd_type & ATR_LBA_EXT)
	{
		lba = (fis_d1 & 0x00FFFFFF) | (GETREG(SATA_FIS_H2D_2) << 24);
		sector_count = fis_d3 & 0x0000FFFF;

		if (sector_count == 0 && (cmd_type & ATR_NO_SECT) == 0)
		{
			sector_count = 0x10000;
		}
	}
	else
	{
		lba = 0;
		sector_count = 0;
	}
	

	if (lba + sector_count > MAX_LBA + 1 && (cmd_type & ATR_NO_SECT) == 0)
		return;

	if (cmd_type & CCL_FTL_H2D)	// Write
	{
		wqueue[WTAIL].lba = lba;
		wqueue[WTAIL].sector_count = sector_count;
		wqueue[WTAIL].cmd_type = 1;
		wqueue[WTAIL].ref = TAIL;
		WTAIL = (WTAIL + 1) % SIZE;

		oqueue[TAIL].lba = lba;
		oqueue[TAIL].sector_count = sector_count;
		oqueue[TAIL].cmd_type = 1;
	}
	else	// Read
	{
		rqueue[RTAIL].lba = lba;
		rqueue[RTAIL].sector_count = sector_count;
		rqueue[RTAIL].cmd_type = 0;
		rqueue[RTAIL].ref = TAIL;
		RTAIL = (RTAIL + 1) % SIZE;

		oqueue[TAIL].lba = lba;
		oqueue[TAIL].sector_count = sector_count;
		oqueue[TAIL].cmd_type = 0;
	}

	TAIL = (TAIL + 1) % (SIZE * 2);

/*
	uart_print("ENQUEUE> SW lba / sect / cmd :"); 
	uart_print_32(lba);
	uart_print_32(sector_count);
	uart_print_32(cmd);
	*/
}

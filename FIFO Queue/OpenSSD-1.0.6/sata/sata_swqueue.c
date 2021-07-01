#include "jasmine.h"
#include "sata.h"

/* sw queue */
#define SIZE 128
CMD_T swqueue[SIZE];
UINT32 HEAD = 0;
UINT32 TAIL = 0;

/* FIFO queue */
// DEQUEUE
 void sw_eventq_pop(CMD_T* cmd)
{
	while ((GETREG(SATA_EQ_DATA_2) & 8) != 0);
	
	cmd->lba = swqueue[HEAD].lba;
	cmd->sector_count = swqueue[HEAD].sector_count;
	cmd->cmd_type = swqueue[HEAD].cmd_type;

	HEAD = (HEAD + 1) % SIZE;
	
	if(cmd->sector_count == 0)
		cmd->sector_count = 0x10000;

	//SETREG(SATA_EQ_STATUS, ((eveq_rear-eveq_front) & 0xFF)<<16);


	uart_print("DEQUEUE> SW lba / sect / cmd :");
	uart_print_32(cmd->lba);
	uart_print_32(cmd->sector_count);
	uart_print_32(cmd->cmd_type);
	uart_print("=============================================="); 
}

// ENQUEUE
 void sw_eventq_push(void)
{
	
	UINT32 lba, sector_count, cmd_code, cmd_type, fis_d1, fis_d3;
	UINT32 cmd;

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
		cmd = 1;
	else	// Read
		cmd = 0;

	swqueue[TAIL].lba = lba;
	swqueue[TAIL].sector_count = sector_count;
	swqueue[TAIL].cmd_type = cmd;

	TAIL = (TAIL + 1) % SIZE;

//SETREG(SATA_EQ_STATUS, ((eveq_rear-eveq_front) & 0xFF)<<16);
/*
	uart_print("ENQUEUE> SW lba / sect / cmd :"); 
	uart_print_32(lba);
	uart_print_32(sector_count);
	uart_print_32(cmd);
	*/
}

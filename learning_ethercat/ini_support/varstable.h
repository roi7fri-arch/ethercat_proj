

// Ini.h: interface for the CIni class.


#ifndef _VARSTABLE_H_
#define _VARSTABLE_H_

struct vars
{
	int cmd;
	int type;
	int size;
	struct vars *next;
};

	int pdo_ini_read_table(void);
	int pdo_ini_init(char *filename);
	int pdo_ini_close(void);
	void display_table(void);
	struct vars *get_vars_head(void);
	int get_list_size(void);
	int *prepare_pdo_array(void);

#endif


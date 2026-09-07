// Copyright 1997-98 (c) Iuri Apollonio
// given freely to www.codeguru.com

// Ini.cpp: implementation of the CIni class.
//
//////////////////////////////////////////////////////////////////////


#include <stdio.h>
#include "varstable.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

FILE *fp;

struct vars *head = 0;

int list_size = 0;

int *pdo_array = NULL; 

int pdo_ini_init(char *filename)
{
	if ((fp = fopen(filename, "rt")) == NULL)
		return 0;
	return 1;
}

int pdo_ini_close(void)
{
	struct vars *curr = head;
	struct vars *next = head;

	while (curr != 0)
	{
		printf("delete 0x%x, %d, %d\n", curr->cmd, curr->type, curr->size);
		next = curr->next;
		free(curr);
		curr = next;
	}

	free(pdo_array);

	if (fp)
		fclose(fp);
}

void display_table(void)
{
	struct vars *p = head;

	while (p != 0)
	{
		printf("display 0x%x, %d, %d\n", p->cmd, p->type, p->size);
		p = p->next;
	}
}

int pdo_ini_read_table(void)
{
	int res = 1;
	char txt[80];
	int cmd;
	int type;
	int size;
	struct vars *p;
	struct vars *tmp;

	fseek(fp, 0, SEEK_SET);
	do
	{
		fgets(txt, 80, fp);
		res = sscanf(txt, "%x %d %d", &cmd, &type, &size);
		if (res == 3)
		{
			if (head == 0)
			{
				head = (struct vars*)malloc(sizeof(struct vars));
				head->cmd = cmd;
				head->type = type;
				head->size = size;
				head->next = 0;
				list_size++;
			}
			else
			{
				p = head;
				while (p->next != 0)
					p = p->next;
				tmp = (struct vars*)malloc(sizeof(struct vars));
				p->next = tmp;
				tmp->cmd = cmd;
				tmp->type = type;
				tmp->size = size;
				tmp->next = 0;
				list_size++;
			}
			printf("add 0x%x, %d, %d\n", cmd, type, size);
		}
	} while (!feof(fp));

	return 1;
}


int get_list_size()
{
	printf("list_size = %d\n", list_size);
	return list_size;
}

int *prepare_pdo_array()
{
	int pdo_list_size = 0;
	int i = 0;
	struct vars *vars_head;
	pdo_list_size = get_list_size();
	pdo_array = (int*)malloc(pdo_list_size * 4);
	vars_head = head;
	for(i = 0; i < pdo_list_size; i++)
	{
		pdo_array[i] = vars_head->cmd;
		pdo_array[i] <<= 8;
		pdo_array[i] += vars_head->type;
		pdo_array[i] <<= 8;
		pdo_array[i] += vars_head->size;
		vars_head = vars_head->next;
	}
	return pdo_array;
}

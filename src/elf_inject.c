#include "elf_inject.h"
#include <elf.h>
#include <unistd.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

blob elf_inject (char* code, size_t code_size, char* elf_buf, size_t elf_buf_size)
{
	Elf64_Ehdr* file_header = (Elf64_Ehdr*)elf_buf;
	Elf64_Phdr* program_tables = (Elf64_Phdr*)(elf_buf + file_header->e_phoff);
	//We not only need to inject the code, but create a new segment for it
	char* new_elf_buf;
	Elf64_Ehdr* new_file_header;
	Elf64_Phdr* new_program_tables;
	int i;
	uint64_t entry_point = file_header->e_entry;
	uint64_t current_offset;
	Elf64_Phdr new_segment;
	Elf64_Phdr* last_loadable = NULL;
	Elf64_Phdr* text_segment;
	blob ret;

	for (i = 0; i < file_header->e_phnum; i ++)
	{
		if (program_tables [i].p_vaddr < entry_point && program_tables [i].p_vaddr + program_tables [i].p_memsz > entry_point)
			text_segment = (Elf64_Phdr*)(new_elf_buf + program_tables [i].p_offset);
		if (program_tables [i].p_type == PT_LOAD && (!last_loadable || program_tables [i].p_vaddr > last_loadable->p_vaddr))
			last_loadable = &program_tables [i];
	}

	//Setup the new segment
	new_segment.p_type = PT_LOAD;
	//Shoutout to the folks on freenode's ##re channel for helping me figure out this alignment thing!
	new_segment.p_offset = elf_buf_size;
	new_segment.p_vaddr = ceil (last_loadable->p_vaddr / (double)getpagesize ()) * getpagesize() + new_segment.p_offset;
	new_segment.p_filesz = code_size;
	new_segment.p_memsz = code_size;
	new_segment.p_flags = PF_R | PF_X;
	new_segment.p_align = getpagesize ();

	//Allocate memory for the new segment
	new_elf_buf = (char*)malloc (new_segment.p_offset + code_size);
	ret.buf_size = new_segment.p_offset + code_size;
	ret.buf = new_elf_buf;

	//Copy everything up to the end of the program tables
	memcpy (new_elf_buf, elf_buf, file_header->e_phoff + sizeof (Elf64_Phdr)*file_header->e_phnum);
	current_offset = file_header->e_phoff + sizeof (Elf64_Phdr)*file_header->e_phnum;

	//Modify the offsets on all the segments since we're adding a new segment
	new_program_tables = (Elf64_Phdr*)(new_elf_buf + file_header->e_phoff);
	for (i = 0; i < file_header->e_phnum; i ++)
	{
		if (new_program_tables [i].p_offset > file_header->e_phoff)
			new_program_tables [i].p_offset += sizeof (Elf64_Phdr);
	}

	//Copy the new segment
	memcpy (new_elf_buf + current_offset, &new_segment, sizeof (Elf64_Phdr));
	current_offset += sizeof (Elf64_Phdr);

	//Copy the rest of the elf
	memcpy (new_elf_buf + current_offset, elf_buf + current_offset - sizeof (Elf64_Phdr), elf_buf_size - current_offset - sizeof (Elf64_Phdr));

	//Copy the new code
	memcpy (new_elf_buf + new_segment.p_offset, code, code_size);

	//Modify the ELF header
	new_file_header = (Elf64_Ehdr*)new_elf_buf;
	new_file_header->e_phnum ++;
	new_file_header->e_entry = new_segment.p_vaddr;

	free (elf_buf);
	return ret;
}

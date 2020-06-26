// The MIT License (MIT)
//
// Copyright (c) 2016, 2017 Trevor Bakker
// Copyright (c) 2020 Himanshu Rijal
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <ctype.h>

#define WHITESPACE " \t\n"      // We want to split our command line up into tokens
                                // so we need to define what delimits our tokens.
                                // In this case  white space
                                // will separate the tokens on our command line

#define MAX_COMMAND_SIZE 255    // The maximum command-line size

#define MAX_NUM_ARGUMENTS 5     // Mav shell only supports five arguments

#define MAX_DIRECTORY_ENTRIES 16 //Maximum directory entries supported is 16

//FAT32 file system image specification
int16_t BPB_BytsPerSec = 0;
int8_t BPB_SecPerClus = 0;
int16_t BPB_RsvdSecCnt = 0;
int8_t BPB_NumFATs = 0;
int16_t BPB_RootEntCnt = 0;
int32_t BPB_FATSz32 = 0;


//Directory specification structure
struct __attribute__((__packed__)) DirectoryEntry
{
    char DIR_Name[11];
    uint8_t DIR_Attr;
    uint8_t Unused1[8];
    uint16_t DIR_FirstClusterHigh;
    uint8_t Unused2[4];
    uint16_t DIR_FirstClusterLow;
    uint32_t DIR_FileSize;
};

struct DirectoryEntry dir[MAX_DIRECTORY_ENTRIES]; //Structure for the current directory

FILE *fp = NULL; //File pointer
char file_closed = 'Y'; //File status
int root_address = 0; //Address of root directory
int directory_path[MAX_DIRECTORY_ENTRIES + 1]; //Array to store the current directory path starting from the root directory
int directory_path_pointer = 0; //Pointer to check which directory the user is currently at in the file system image


//FUNCTIONS
int LBAToOffset( int32_t sector );
int16_t NextLB( uint32_t sector );
int compare(char input[]);


int main()
{
  char * cmd_str = (char*) malloc( MAX_COMMAND_SIZE );

  while( 1 )
  {
    // Print out the mfs prompt
    printf ("mfs> ");

    //Read the command from the commandline.  The
    //maximum command that will be read is MAX_COMMAND_SIZE
    //This while command will wait here until the user
    //inputs something since fgets returns NULL when there
    //is no input
    while( !fgets (cmd_str, MAX_COMMAND_SIZE, stdin) );

    //Parse input
    char *token[MAX_NUM_ARGUMENTS];

    int token_count = 0;
                                                           
    // Pointer to point to the token
    // parsed by strsep
    char *arg_ptr;
                                                           
    char *working_str  = strdup( cmd_str );

    //We are going to move the working_str pointer so
    //keep track of its original value so we can deallocate
    //the correct amount at the end
    char *working_root = working_str;

    //Tokenize the input stringswith whitespace used as the delimiter
    while ( ( (arg_ptr = strsep(&working_str, WHITESPACE) ) != NULL ) &&
              ( token_count<MAX_NUM_ARGUMENTS ) )
    {
      token[token_count] = strndup( arg_ptr, MAX_COMMAND_SIZE );
      if( strlen( token[token_count] ) == 0 )
      {
        token[token_count] = NULL;
      }
        token_count++;
    }

    if( fp == NULL && strcmp(token[0],"open") == 0 ) //If file system image hasn't been opened
    {
        if( ( fp = fopen(token[1],"r")) == NULL )
        {
            printf("Error: File system image not found.\n");
        }
        else
        {
            file_closed = 'N'; //This file has not been closed
            
            //Load values for file system specification variables when we first open the file
            
            fseek(fp,11,SEEK_SET); //Location: Offset byte 11
            fread(&BPB_BytsPerSec,2,1,fp); //Get the number of bytes in one sector
            
            fseek(fp,13,SEEK_SET); //Location: Offset byte 13
            fread(&BPB_SecPerClus,1,1,fp); //Get the number of sectors in one allocation unit
            
            fseek(fp,14,SEEK_SET); //Location: Offset byte  14
            fread(&BPB_RsvdSecCnt,2,1,fp); //Get the  number of reserved sectors  in the Reserved region of the volume
            
            fseek(fp,16,SEEK_SET); //Location: Offset byte 16
            fread(&BPB_NumFATs,1,1,fp); //Get the count of FAT data structures in the volume
            
            fseek(fp,17,SEEK_SET); //Location: Offset byte 17
            fread(&BPB_RootEntCnt,2,1,fp); //Get the count of 32 byte directory entries in the root directory (FAT 12 and 16 volumes),
                                                //value should be 0 for FAT 32 volumes
            
            fseek(fp,36,SEEK_SET); //Location: Offset byte 36
            fread(&BPB_FATSz32,4,1,fp); //Get the 32 bit count of sectors occupied by ONE FAT (Only defined for FAT 32, 0 for rest)
            
            
            //Calculate the address of the root directory in the file and load as current directory
            
            root_address = (BPB_NumFATs * BPB_FATSz32 * BPB_BytsPerSec) +
                                    (BPB_RsvdSecCnt * BPB_BytsPerSec); // Get the root addres
            
            directory_path[directory_path_pointer] = root_address;
            directory_path_pointer++;
            
            fseek(fp,root_address,SEEK_SET);
            fread(&dir[0],MAX_DIRECTORY_ENTRIES,sizeof(struct DirectoryEntry),fp);
        }
    }
    else if( fp != NULL && strcmp(token[0],"open") == 0 ) //If file system image has alreadys been opened display error message
    {
        printf("Error: File system image already open\n");
    }
    else if( strcmp(token[0],"close") == 0 ) //Close the  file
    {
        if( fclose(fp) != 0 )
        {
            printf("Error: File system image not found\n");
        }
        else
        {
            printf("Use quit to exit the program. \n");
            fp = NULL;
            file_closed = 'Y';
        }
    }
    else if( strcmp(token[0],"quit") == 0 )
    {
        if( fp == NULL )
        {
            exit(0);
        }
        else
        {
            fclose(fp);
            fp = NULL;
            file_closed = 'Y';
            exit(0);
        }
    }
    else if( file_closed == 'Y' && strlen(*token) > 0 ) //If file system image has been closed but user issues a command
    {
        printf("Error: File system image must be open first\n");
    }
    else if( strcmp(token[0],"info") == 0 ) //Print information about the specifications of the file system image
    {
        printf(" BPB_BytsPerSec: %d\n BPB_BytsPerSec: %x\n\n BPB_SecPerClus: %d\n BPB_SecPerClus: %x\n\n BPB_RsvdSecCnt: %d\n BPB_RsvdSecCnt: %x\n\n BPB_NumFATs: %d\n BPB_NumFATs: %x\n\n BPB_FATSz32: %d\n BPB_FATSz32: %x\n\n",
                 BPB_BytsPerSec,BPB_BytsPerSec,
                 BPB_SecPerClus,BPB_SecPerClus,
                 BPB_RsvdSecCnt,BPB_RsvdSecCnt,
                 BPB_NumFATs,BPB_NumFATs,
                 BPB_FATSz32,BPB_FATSz32);
    }
    else if( strcmp(token[0],"stat") == 0 ) //Displays the attributes and
                                            //the starting cluster number of the file or directory name
    {
        char input[MAX_COMMAND_SIZE]; //String to store file name input from user
        strcpy(input,token[1]);
        
        int position = compare(input); //Get the position of the file or sub-directory in the file system image
        
        if( position == MAX_DIRECTORY_ENTRIES ) //If file or directory couldn't be found
        {
            printf("Error: File not found\n");
        }
        else
        {
            printf("Attribute: %x\nSize: %x\nStarting Cluster Number:%x\n",
                dir[position].DIR_Attr,dir[position].DIR_FileSize,dir[position].DIR_FirstClusterLow);
        }
    }
    else if( strcmp(token[0],"get") == 0 ) //Print all the required information about the file system image
    {
        char input[MAX_COMMAND_SIZE]; //String to store file name input from user
        char input_copy[MAX_COMMAND_SIZE]; //Copy of input string
        
        strcpy(input,token[1]);
        strcpy(input_copy,token[1]);
        
        int position = compare(input); //Get the position of the file in the file system image
        
        if( position == MAX_DIRECTORY_ENTRIES )
        {
            printf("Error: File not found\n");
        }
        else
        {
            //Specification for file in the file system image to be moved to the current local working directory
            
            FILE *new_file_fp; //File pointer for file to be placed in current working directory
            int offset; //Starting address for file
            int next_block_address; //Logical adress for next cluster block
            int next_block_offset; //Offset of next block in the file system image
            int total_clusters = ( dir[position].DIR_FileSize % BPB_BytsPerSec ) == 0 ?
                                        ( dir[position].DIR_FileSize / BPB_BytsPerSec ) :
                                            (dir[position].DIR_FileSize / BPB_BytsPerSec ) + 1; //Total number of clusters for the file; //Total number of clusters for the file
            int rem_byts_last_cluster = BPB_BytsPerSec -
                                    ( total_clusters * BPB_BytsPerSec - dir[position].DIR_FileSize ); //Total bytes                                                                        in the last cluster of the file
            char file_data[BPB_BytsPerSec+1]; //Array to store file data
            
            if(dir[position].DIR_FileSize <= BPB_BytsPerSec) //If the total file fits only one cluster
            {
                offset = LBAToOffset(dir[position].DIR_FirstClusterLow);
                fseek(fp,offset,SEEK_SET);
                fread(file_data,dir[position].DIR_FileSize,1,fp);
                file_data[dir[position].DIR_FileSize] = '\0';
                new_file_fp = fopen(input_copy,"w+");
                fprintf(new_file_fp,"%s", file_data);
                fclose(new_file_fp);
            }
            else //If the file spans across two or  more clusters
            {
                offset = LBAToOffset(dir[position].DIR_FirstClusterLow);
                fseek(fp,offset,SEEK_SET);
                fread(file_data,BPB_BytsPerSec,1,fp);
                file_data[BPB_BytsPerSec] = '\0';
                new_file_fp = fopen(input_copy,"w+");
                fprintf(new_file_fp,"%s", file_data);

                next_block_address = NextLB(dir[position].DIR_FirstClusterLow);
                next_block_offset = LBAToOffset(next_block_address);
                
                while( next_block_address != -1 ) //Go over all the clusters and copy data from the file in the file system                                         image to the new file  in the current working directory
                {
                    if( NextLB(next_block_address) == -1 )
                    {
                        char file_data_last[rem_byts_last_cluster+1]; //Array to store file data
                        fseek(fp,next_block_offset,SEEK_SET);
                        fread(file_data_last,rem_byts_last_cluster,1,fp);
                        file_data[rem_byts_last_cluster] = '\0';
                        fprintf(new_file_fp,"%s", file_data_last);
                    }
                    else
                    {
                        fseek(fp,next_block_offset,SEEK_SET);
                        fread(file_data,BPB_BytsPerSec,1,fp);
                        file_data[BPB_BytsPerSec] = '\0';
                        fprintf(new_file_fp,"%s", file_data);
                    }
                    next_block_address = NextLB(next_block_address);
                    next_block_offset = LBAToOffset(next_block_address);
                }
                fclose(new_file_fp);
            }
        }
    }
    else if( strcmp(token[0],"cd") == 0 ) //Change directories
    {
        if( token[1] == NULL || (token[1] != NULL && strcmp(token[1],".") == 0) )
        {
            directory_path_pointer = 1;
            
            fseek(fp,root_address,SEEK_SET);
            fread(&dir[0],16,sizeof(struct DirectoryEntry),fp); //Set the previous directory as the current directory
        }
        else
        {
            // Parse input
            char * input[MAX_COMMAND_SIZE];

            int input_count = 0;
                                                                   
            //Pointer to point to the token
            //parsed by strsep
            char *arg_ptr;
                                                                   
            char *working_str  = strdup( token[1] );

            //We are going to move the working_str pointer so
            //keep track of its original value so we can deallocate
            //the correct amount at the end
            char *working_root = working_str;

            //Tokenize the input strings with / used as the delimiter
            while ( ( (arg_ptr = strsep(&working_str, "/" ) ) != NULL) &&
                      (token_count<MAX_NUM_ARGUMENTS))
            {
              input[input_count] = strndup( arg_ptr, MAX_COMMAND_SIZE );
              if( strlen( input[input_count] ) == 0 )
              {
                input[input_count] = NULL;
              }
                input_count++;
            }
            
            int token_index  = 0;
            for( token_index = 0; token_index < input_count; token_index ++ )
            {
                if( strcmp(input[token_index],".") == 0)
                {
                    directory_path_pointer = 1;
                    
                    fseek(fp,root_address,SEEK_SET);
                    fread(&dir[0],16,sizeof(struct DirectoryEntry),fp); //Set the previous directory as the current directorys
                }
                else if( strcmp(input[token_index],"..") == 0 )
                {
                    if( directory_path_pointer - 1 == 0 )
                    {
                        printf("Already at root directory.\n");
                    }
                    else
                    {
                        directory_path_pointer--;
                        
                        int offset = directory_path[directory_path_pointer - 1] == root_address ? root_address : LBAToOffset(directory_path[directory_path_pointer - 1]); //If changing directory to root
                                                                                     //then we don't need to calculate offset
                        
                        fseek(fp,offset,SEEK_SET);
                        fread(&dir[0],16,sizeof(struct DirectoryEntry),fp); //Set the previous directory as the current directory
                    }
                }
                else
                {
                    int position = compare(input[token_index]); //Get the position of the sub-directory in the file system image
                    
                    if( position == MAX_DIRECTORY_ENTRIES )
                    {
                        printf("%s: No such file or directory.\n",input[token_index]);
                        break;
                    }
                    else //If sub-directory exists change it to current directory and update directory path
                    {
                        directory_path[directory_path_pointer] = dir[position].DIR_FirstClusterLow;
                        directory_path_pointer++;
                        
                        int offset = LBAToOffset(dir[position].DIR_FirstClusterLow);
                        
                        fseek(fp,offset,SEEK_SET);
                        fread(&dir[0],16,sizeof(struct DirectoryEntry),fp); //Set the sub-directory as the current directory
                    }
                }
            }
            
            free(working_root);
        }
    }
    else if( strcmp(token[0],"ls") == 0 ) //List the files and sub-directories in the current directory
    {
        if( token[1] != NULL && strcmp(token[1],"..") == 0 )
        {
            if( directory_path_pointer - 1 == 0 )
            {
                printf("Already at root directory. Use ls\n");
            }
            else //Create a temporary directory structure to hold preceding directory data
            {
                struct DirectoryEntry temp_dir[MAX_DIRECTORY_ENTRIES]; //Structure for the temporary directory
                int temp_directory_path_pointer = directory_path_pointer - 1; //Temporary directory path pointer
                int offset = directory_path[temp_directory_path_pointer - 1] == root_address ? root_address : LBAToOffset(directory_path[temp_directory_path_pointer - 1]); //If changing directory to root
                                                                             //then we don't need to calculate offset
                fseek(fp,offset,SEEK_SET);
                fread(&temp_dir[0],16,sizeof(struct DirectoryEntry),fp); //Set the previous directory as the current directory
                
                int i = 0; //Loop variable
                int j = 0; //Loop variable
                
                for ( i = 0; i < MAX_DIRECTORY_ENTRIES; i++)
                {
                    char substring[12];
                    for ( j = 0; j < 11; j++)
                    {
                        substring[j] = temp_dir[i].DIR_Name[j];
                    }
                    substring[11] = '\0'; //Create substring of file name for each file in directory entry array
                    
                    if ((temp_dir[i].DIR_Attr == 0x01 || temp_dir[i].DIR_Attr == 0x10
                        || temp_dir[i].DIR_Attr == 0x20) && temp_dir[i].DIR_Name[0] != '\xE5')
                    {
                        printf("%s\n",substring);
                    }
                }
            }
        }
        else
        {
            int i = 0;
            int j = 0;
            
            for ( i = 0; i < MAX_DIRECTORY_ENTRIES; i++ )
            {
                char substring[12];
                for ( j = 0; j < 11; j++)
                {
                    substring[j] = dir[i].DIR_Name[j];
                }
                substring[11] = '\0'; //Create substring of file name for each file in directory entry array
                
                if ( ( dir[i].DIR_Attr == 0x01 || dir[i].DIR_Attr == 0x10
                    || dir[i].DIR_Attr == 0x20 ) && dir[i].DIR_Name[0] != '\xE5' )
                {
                    printf("%s\n",substring);
                }
            }
        }
        
    }
    else if( strcmp(token[0],"read") == 0 ) //Print all the required information about the file system image
    {

        char input[255]; //String to store file name input from user
        int start_position = atoi(token[2]); //Position in file to start reading
        int num_bytes = atoi(token[3]); //Number of bytes given position in file to read
        int k = 0; //Loop variable
        
        strcpy(input,token[1]);
        
        int directory_position = compare(input);

        if( directory_position == MAX_DIRECTORY_ENTRIES )
        {
            printf("Error: File not found.\n");
        }
        else if( start_position + num_bytes > dir[directory_position].DIR_FileSize )
        {
            printf("Error: Number of bytes to be read exceeds file size.\n");
        }
        else
        {
            char result[num_bytes+1];  //Preprare char array with length 1 greater than no. of bytes to be read
            
            if( num_bytes <= BPB_BytsPerSec)
            {
                 int offset = LBAToOffset(dir[directory_position].DIR_FirstClusterLow);
                
                 fseek(fp,offset + start_position,SEEK_SET);
                 fread(result,num_bytes,1,fp);
            }
            else
            {
                int offset = LBAToOffset(dir[directory_position].DIR_FirstClusterLow);
                int next_block_address; //Logical block address for next cluster block
                int next_block_offset; //Offset of next block in the file system image
                int read_position_pointer = 0; //Store position for reading bytes into result array
                
                fseek(fp,offset,SEEK_SET);
                fread(&result[read_position_pointer],BPB_BytsPerSec,1,fp);
                read_position_pointer += BPB_BytsPerSec;
                
                next_block_address = NextLB(dir[directory_position].DIR_FirstClusterLow);
                next_block_offset = LBAToOffset(next_block_address);
                
                while( num_bytes > read_position_pointer ) //Loop stops after total bytes to be read is less than total cluster size read. For eg: If num_bytes = 513 then loop stops when read_position_pointer = 1024 after 1 iteration
                {
                    if( num_bytes <= read_position_pointer + BPB_BytsPerSec ) //Read the remaining bytes
                    {
                        fseek(fp,next_block_offset,SEEK_SET);
                        fread(&result[read_position_pointer], num_bytes - read_position_pointer ,1,fp);
                        result[num_bytes] = '\0';
                        read_position_pointer += BPB_BytsPerSec;
                        //printf("1. %s", file_data_last);
                    }
                    else
                    {
                        fseek(fp,next_block_offset,SEEK_SET);
                        fread(&result[read_position_pointer],BPB_BytsPerSec,1,fp);
                        read_position_pointer += BPB_BytsPerSec;
                    }
                    next_block_address = NextLB(next_block_address);
                    next_block_offset = LBAToOffset(next_block_address);
                }
            }

             while( k != num_bytes ) // Print the output as hex characters in the file
             {
                 printf("%x ",result[k]);
                 k++;
             }
             printf("\n");
        }
    }
    else
    {
        printf("Error: Command not supported\n");
    }
      
    free( working_root );
  }
    
  return 0;
}


// Finds the starting address of a block of data given the sector number corresponding to that data block

int LBAToOffset(int32_t sector )
{
    return ( ( sector - 2 ) * BPB_BytsPerSec ) + ( BPB_BytsPerSec *  BPB_RsvdSecCnt )
                + ( BPB_NumFATs * BPB_FATSz32 * BPB_BytsPerSec );
}


// Given a logical block address, lookup into the first FAT and return the logical address of the next block in file

int16_t NextLB( uint32_t sector )
{
    uint32_t FATAddress = ( BPB_BytsPerSec * BPB_RsvdSecCnt ) + (sector * 4 );
    int16_t val;
    fseek( fp, FATAddress, SEEK_SET );
    fread( &val, 2, 1, fp);
    return val;
}

// Convert input file name or sub-directory name into format from file system image and return the position.
// Function will return MAX_DIRECCTORY_ENTRIES value if the file or sub-directory cannot be found.

int compare(char input[])
{
    //Convert input file name into format from file system image
    
    int i = 0; //Loop variable
    int j = 0; //Loop variable

    char expanded_name[MAX_COMMAND_SIZE];
    memset( expanded_name, ' ', 12 );

    char *token = strtok( input, "." );

    strncpy( expanded_name, token, strlen( token ) );

    token = strtok( NULL, "." );

    if( token )
    {
      strncpy( (char*)(expanded_name+8), token, strlen(token ) );
    }

    expanded_name[11] = '\0';

    for(i = 0; i < 11; i++)
    {
      expanded_name[i] = toupper( expanded_name[i] );
    }
    
    //Check if file or sub-directory exists in the current directory
    
    for (i = 0; i < MAX_DIRECTORY_ENTRIES; i++)
    {
        char substring[12];
        for ( j = 0; j < 11; j++)
        {
            substring[j] = dir[i].DIR_Name[j];
        }
        substring[11] = '\0'; //Create substring of file name for each file in directory entry array
        
        if( strcmp(substring,expanded_name) == 0 )
        {
            break;
        }
    }
    
    return i;
}

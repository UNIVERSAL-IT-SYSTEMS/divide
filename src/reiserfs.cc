/* Copyright (C) 2004 Bart
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
 
 
#include "../include/reiserfs.h"

namespace GParted
{

FS reiserfs::get_filesystem_support( )
{
	FS fs ;
	
	fs .filesystem = "reiserfs" ;
	
	if ( ! system( "which debugreiserfs 1>/dev/null 2>/dev/null" ) ) 
		fs .read = true ;
	
	if ( ! system( "which mkreiserfs 1>/dev/null 2>/dev/null" ) ) 
		fs .create = true ;
	
	if ( ! system( "which reiserfsck 1>/dev/null 2>/dev/null" ) ) 
		fs .check = true ;
		
	//resizing is a delicate process which requires 3 commands..
	if ( ! system( "which resize_reiserfs 1>/dev/null 2>/dev/null" ) && fs .read && fs .check ) 
		fs .resize = true ;
	
	//we need to call resize_reiserfs after a copy to get proper used/unused
	if ( ! system( "which dd 1>/dev/null 2>/dev/null" ) && fs .resize ) 
		fs .copy = true ;
	
	return fs ;
}

void reiserfs::Set_Used_Sectors( Partition & partition ) 
{
	char c_buf[ 512 ] ;
	FILE *f ;
	
	Glib::ustring output ;
	Sector free_blocks = -1, blocksize = -1 ;

        //get free blocks..
	f = popen( ( "debugreiserfs " + partition .partition ) .c_str( ), "r" ) ;
	while ( fgets( c_buf, 512, f ) )
	{
		output = Glib::locale_to_utf8( c_buf ) ;
		
		//blocksize
		if ( output .find( "Blocksize" ) < output .length( ) )
			blocksize = atoi( (output .substr( output .find( ":" ) +1, output .length( ) ) ) .c_str( ) ) ;
		
		//free blocks
		if ( output .find( "Free blocks" ) < output .length( ) )
			free_blocks = atoi( (output .substr( output .find( ":" ) +1, output .length( ) ) ) .c_str( ) ) ;
	}
	pclose( f ) ;
	
	if ( free_blocks > -1 && blocksize > -1 )
		partition .Set_Unused( free_blocks * blocksize / 512 ) ;
}
	
bool reiserfs::Create( const Glib::ustring device_path, const Partition & new_partition )
{
	return Execute_Command( "mkreiserfs -q " + new_partition .partition ) ;
}

bool reiserfs::Resize( const Partition & partition_new, bool fill_partition )
{
	Glib::ustring str_temp = "echo y | resize_reiserfs " + partition_new .partition ;
	
	if ( ! fill_partition )
		str_temp += " -s " + num_to_str( partition_new .Get_Length_MB( ) - cylinder_size ) + "M" ;
	
	return Execute_Command( str_temp ) ;
}

bool reiserfs::Copy( const Glib::ustring & src_part_path, const Glib::ustring & dest_part_path )
{
	//copy partition
	Execute_Command( "dd bs=8192 if=" + src_part_path + " of=" + dest_part_path ) ;
	
	//and set proper used/unused
	return Execute_Command( "resize_reiserfs " + dest_part_path ) ;
}

bool reiserfs::Check_Repair( const Partition & partition )
{
	return Execute_Command( "reiserfsck -y --fix-fixable " + partition .partition ) ;
}

int reiserfs::get_estimated_time( long MB_to_Consider )
{
	return -1 ;
}
	


} //GParted
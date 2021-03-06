/* Copyright (C) 2004 Bart
 * Copyright (C) 2008, 2009, 2010 Curtis Gedak
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
 
 
#include "../include/ntfs.h"

namespace GParted
{

FS ntfs::get_filesystem_support()
{
	FS fs ;
	fs .filesystem = GParted::FS_NTFS ;

	if ( ! Glib::find_program_in_path( "ntfsresize" ) .empty() )
	{
		fs .read = GParted::FS::EXTERNAL ;
		fs .check = GParted::FS::EXTERNAL ;
	}

	if ( ! Glib::find_program_in_path( "ntfslabel" ) .empty() ) {
		fs .read_label = FS::EXTERNAL ;
		fs .write_label = FS::EXTERNAL ;
	}

	if ( ! Glib::find_program_in_path( "mkntfs" ) .empty() )
		fs .create = GParted::FS::EXTERNAL ;

	//resizing is a delicate process ...
	if ( fs .read && fs .check )
	{
		fs .grow = GParted::FS::EXTERNAL ;
		
		if ( fs .read ) //needed to determine a min file system size..
			fs .shrink = GParted::FS::EXTERNAL ;
	}

	//we need ntfsresize to set correct used/unused after cloning
	if ( ! Glib::find_program_in_path( "ntfsclone" ) .empty() )
		fs .copy = GParted::FS::EXTERNAL ;

	if ( fs .check )
		fs .move = GParted::FS::GPARTED ;
	
	return fs ;
}

void ntfs::set_used_sectors( Partition & partition ) 
{
	if ( ! Utils::execute_command( 
		"ntfsresize --info --force --no-progress-bar " + partition .get_path(), output, error, true ) )
	{
		index = output .find( "resize at" ) ;
		if ( index >= output .length() ||
		     sscanf( output .substr( index ) .c_str(), "resize at %Ld", &N ) != 1 )
			N = -1 ;

		if ( N > -1 )
			partition .set_used( Utils::round( N / double(partition .sector_size) ) ) ; 
	}
	else
	{
		if ( ! output .empty() )
			partition .messages .push_back( output ) ;
		
		if ( ! error .empty() )
			partition .messages .push_back( error ) ;
	}
}

void ntfs::read_label( Partition & partition )
{
	if ( ! Utils::execute_command( "ntfslabel --force " + partition .get_path(), output, error, true ) )
	{
		partition .label = Utils::regexp_label( output, "^(.*)" ) ;
	}
	else
	{
		if ( ! output .empty() )
			partition .messages .push_back( output ) ;
		
		if ( ! error .empty() )
			partition .messages .push_back( error ) ;
	}
}

bool ntfs::write_label( const Partition & partition, OperationDetail & operationdetail )
{
	return ! execute_command( "ntfslabel --force " + partition .get_path() + " \"" + partition .label + "\"", operationdetail ) ;
}

bool ntfs::create( const Partition & new_partition, OperationDetail & operationdetail )
{
	return ! execute_command( "mkntfs -Q -v -L \"" + new_partition .label + "\" " + new_partition .get_path(), operationdetail ) ;
}

bool ntfs::resize( const Partition & partition_new, OperationDetail & operationdetail, bool fill_partition )
{
	bool return_value = false ;
	Glib::ustring str_temp = "ntfsresize -P --force " + partition_new .get_path() ;
	
	if ( ! fill_partition )
	{
		str_temp += " -s " ;
		str_temp += Utils::num_to_str( Utils::round( Utils::sector_to_unit(
				partition_new .get_sector_length(), partition_new .sector_size, UNIT_BYTE ) ) -1 ) ;
	}
	
	//simulation..
	operationdetail .add_child( OperationDetail( _("run simulation") ) ) ;

	if ( ! execute_command( str_temp + " --no-action", operationdetail .get_last_child() ) )
	{
		operationdetail .get_last_child() .set_status( STATUS_SUCCES ) ;

		//real resize
		operationdetail .add_child( OperationDetail( _("real resize") ) ) ;

		if ( ! execute_command( str_temp, operationdetail .get_last_child() ) )
		{
			operationdetail .get_last_child() .set_status( STATUS_SUCCES ) ;
			return_value = true ;
		}
		else
		{
			operationdetail .get_last_child() .set_status( STATUS_ERROR ) ;
		}
	}
	else
	{
		operationdetail .get_last_child() .set_status( STATUS_ERROR ) ;
	}
	
	return return_value ;
}

bool ntfs::move( const Partition & partition_new
               , const Partition & partition_old
               , OperationDetail & operationdetail
               )
{
	return true ;
}

bool ntfs::copy( const Glib::ustring & src_part_path,
		 const Glib::ustring & dest_part_path, 
		 OperationDetail & operationdetail )
{
	return ! execute_command( "ntfsclone -f --overwrite " + dest_part_path + " " + src_part_path, operationdetail ) ;
}

bool ntfs::check_repair( const Partition & partition, OperationDetail & operationdetail )
{
	return ! execute_command( "ntfsresize -P -i -f -v " + partition .get_path(), operationdetail ) ; 
}

} //GParted



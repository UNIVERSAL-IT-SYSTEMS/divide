/* Copyright (C) 2004 Bart 'plors' Hakvoort
 * Copyright (C) 2008, 2009, 2010, 2011 Curtis Gedak
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
 
#include "../include/Win_GParted.h"
#include "../include/GParted_Core.h"
#include "../include/DMRaid.h"
#include "../include/SWRaid.h"
#include "../include/FS_Info.h"
#include "../include/OperationCopy.h"
#include "../include/OperationCreate.h"
#include "../include/OperationDelete.h"
#include "../include/OperationFormat.h"
#include "../include/OperationResizeMove.h"
#include "../include/OperationLabelPartition.h"
#include "../include/Proc_Partitions_Info.h"

#include "../include/btrfs.h"
#include "../include/ext2.h"
#include "../include/ext3.h"
#include "../include/ext4.h"
#include "../include/fat16.h"
#include "../include/fat32.h"
#include "../include/linux_swap.h"
#include "../include/reiserfs.h"
#include "../include/ntfs.h"
#include "../include/xfs.h"
#include "../include/jfs.h"
#include "../include/hfs.h"
#include "../include/hfsplus.h"
#include "../include/reiser4.h"
#include "../include/ufs.h"
#include <set>
#include <cerrno>
#include <cstring>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <mntent.h>

std::vector<Glib::ustring> libparted_messages ; //see ped_exception_handler()

namespace GParted
{

GParted_Core::GParted_Core() 
{
	lp_device = NULL ;
	lp_disk = NULL ;
	lp_partition = NULL ;
	p_filesystem = NULL ;
	thread_status_message = "" ;

	ped_exception_set_handler( ped_exception_handler ) ; 

	//get valid flags ...
	for ( PedPartitionFlag flag = ped_partition_flag_next( static_cast<PedPartitionFlag>( NULL ) ) ;
	      flag ;
	      flag = ped_partition_flag_next( flag ) )
		flags .push_back( flag ) ;
	
	//throw libpartedversion to the stdout to see which version is actually used.
	std::cout << "======================" << std::endl ;
	std::cout << "libparted : " << ped_get_version() << std::endl ;
	std::cout << "======================" << std::endl ;

	//initialize file system list
	find_supported_filesystems() ;
}

void GParted_Core::find_supported_filesystems()
{
	FILESYSTEMS .clear() ;
	
	FS fs_notsupp;

	btrfs fs_btrfs;
	FILESYSTEMS .push_back( fs_btrfs .get_filesystem_support() ) ;

	ext2 fs_ext2;
	FILESYSTEMS .push_back( fs_ext2 .get_filesystem_support() ) ;
	
	ext3 fs_ext3;
	FILESYSTEMS .push_back( fs_ext3 .get_filesystem_support() ) ;

	ext4 fs_ext4;
	FILESYSTEMS .push_back( fs_ext4 .get_filesystem_support() ) ;

	fat16 fs_fat16;
	FILESYSTEMS .push_back( fs_fat16 .get_filesystem_support() ) ;
	
	fat32 fs_fat32;
	FILESYSTEMS .push_back( fs_fat32 .get_filesystem_support() ) ;
	
	hfs fs_hfs;
	FILESYSTEMS .push_back( fs_hfs .get_filesystem_support() ) ;
	
	hfsplus fs_hfsplus;
	FILESYSTEMS .push_back( fs_hfsplus .get_filesystem_support() ) ;
	
	jfs fs_jfs;
	FILESYSTEMS .push_back( fs_jfs .get_filesystem_support() ) ;
	
	linux_swap fs_linux_swap;
	FILESYSTEMS .push_back( fs_linux_swap .get_filesystem_support() ) ;

	ntfs fs_ntfs;
	FILESYSTEMS .push_back( fs_ntfs .get_filesystem_support() ) ;
	
	reiser4 fs_reiser4;
	FILESYSTEMS .push_back( fs_reiser4 .get_filesystem_support() ) ;
	
	reiserfs fs_reiserfs;
	FILESYSTEMS .push_back( fs_reiserfs .get_filesystem_support() ) ;
	
	ufs fs_ufs;
	FILESYSTEMS .push_back( fs_ufs .get_filesystem_support() ) ;

	xfs fs_xfs;
	FILESYSTEMS .push_back( fs_xfs .get_filesystem_support() ) ;

	//lvm2 physical volume -- not a file system
	fs_notsupp .filesystem = GParted::FS_LVM2 ;
	FILESYSTEMS .push_back( fs_notsupp ) ;

	//luks encryption-- not a file system
	fs_notsupp .filesystem = GParted::FS_LUKS ;
	FILESYSTEMS .push_back( fs_notsupp ) ;

	//unknown file system (default when no match is found)
	fs_notsupp .filesystem = GParted::FS_UNKNOWN ;
	FILESYSTEMS .push_back( fs_notsupp ) ;
}

void GParted_Core::set_user_devices( const std::vector<Glib::ustring> & user_devices ) 
{
	this ->device_paths = user_devices ;
	this ->probe_devices = ! user_devices .size() ;
}
	
void GParted_Core::set_devices( std::vector<Device> & devices )
{
	devices .clear() ;
	Device temp_device ;
	Proc_Partitions_Info pp_info( true ) ;  //Refresh cache of proc partition information
	FS_Info fs_info( true ) ;  //Refresh cache of file system information
	DMRaid dmraid( true ) ;    //Refresh cache of dmraid device information
	SWRaid swraid( true ) ;    //Refresh cache of swraid device information
	
	init_maps() ;
	
	//only probe if no devices were specified as arguments..
	if ( probe_devices )
	{
		device_paths .clear() ;

		//FIXME:  When libparted bug 194 is fixed, remove code to read:
		//           /proc/partitions
		//        This was a problem with no floppy drive yet BIOS indicated one existed.
		//        http://parted.alioth.debian.org/cgi-bin/trac.cgi/ticket/194
		//
		//try to find all available devices if devices exist in /proc/partitions
		std::vector<Glib::ustring> temp_devices = pp_info .get_device_paths() ;
		if ( ! temp_devices .empty() )
		{
			//Try to find all devices in /proc/partitions
			for (unsigned int k=0; k < temp_devices .size(); k++)
			{
				/*TO TRANSLATORS: looks like Scanning /dev/sda */
				set_thread_status_message( String::ucompose ( _("Scanning %1"), temp_devices[ k ] ) ) ;
				ped_device_get( temp_devices[ k ] .c_str() ) ;
			}

			//Try to find all swraid devices
			if (swraid .is_swraid_supported() ) {
				std::vector<Glib::ustring> swraid_devices ;
				swraid .get_devices( swraid_devices ) ;
				for ( unsigned int k=0; k < swraid_devices .size(); k++ ) {
					set_thread_status_message( String::ucompose ( _("Scanning %1"), swraid_devices[k] ) ) ;
					ped_device_get( swraid_devices[k] .c_str() ) ;
				}
			}

			//Try to find all dmraid devices
			if (dmraid .is_dmraid_supported() ) {
				std::vector<Glib::ustring> dmraid_devices ;
				dmraid .get_devices( dmraid_devices ) ;
				for ( unsigned int k=0; k < dmraid_devices .size(); k++ ) {
					set_thread_status_message( String::ucompose ( _("Scanning %1"), dmraid_devices[k] ) ) ;
#ifndef USE_LIBPARTED_DMRAID
					dmraid .create_dev_map_entries( dmraid_devices[k] ) ;
					settle_device( 1 ) ;
#endif
					ped_device_get( dmraid_devices[k] .c_str() ) ;
				}
			}
		}
		else
		{
			//No devices found in /proc/partitions so use libparted to probe devices
			ped_device_probe_all();
		}

		lp_device = ped_device_get_next( NULL );
		while ( lp_device ) 
		{
			//only add this device if we can read the first sector (which means it's a real device)
			char * buf = static_cast<char *>( malloc( lp_device ->sector_size ) ) ;
			if ( buf )
			{
				/*TO TRANSLATORS: looks like Confirming /dev/sda */ 
				set_thread_status_message( String::ucompose ( _("Confirming %1"), lp_device ->path ) ) ;
				if ( ped_device_open( lp_device ) )
				{
#ifdef HAVE_LIBPARTED_2_2_0_PLUS
					//Devices with sector sizes of 512 bytes and higher are supported
					if ( ped_device_read( lp_device, buf, 0, 1 ) )
						device_paths .push_back( lp_device ->path ) ;
#else
					//Only devices with sector sizes of 512 bytes are well supported
					if ( lp_device ->sector_size != 512 )
					{
						/*TO TRANSLATORS: looks like  Ignoring device /dev/sde with logical sector size of 2048 bytes. */
						Glib::ustring msg = String::ucompose ( _("Ignoring device %1 with logical sector size of %2 bytes."), lp_device ->path, lp_device ->sector_size ) ;
						msg += "\n" ;
						msg += _("GParted requires libparted version 2.2 or higher to support devices with sector sizes larger than 512 bytes.") ;
						std::cout << msg << std::endl << std::endl ;
					}
					else
					{
						if ( ped_device_read( lp_device, buf, 0, 1 ) )
							device_paths .push_back( lp_device ->path ) ;
					}
#endif
					ped_device_close( lp_device ) ;
				}
				free( buf ) ;
			}
			lp_device = ped_device_get_next( lp_device ) ;
		}
		close_device_and_disk() ;

		std::sort( device_paths .begin(), device_paths .end() ) ;
	}
#ifndef USE_LIBPARTED_DMRAID
	else
	{
		//Device paths were passed in on the command line.

		//Ensure that dmraid device entries are created
		for ( unsigned int t = 0 ; t < device_paths .size() ; t++ ) 
		{
			if ( dmraid .is_dmraid_supported() &&
			     dmraid .is_dmraid_device( device_paths[t] ) )
			{
				dmraid .create_dev_map_entries( dmraid .get_dmraid_name( device_paths [t] ) ) ;
			}
		}
	}
#endif

	for ( unsigned int t = 0 ; t < device_paths .size() ; t++ ) 
	{
		/*TO TRANSLATORS: looks like Searching /dev/sda partitions */ 
		set_thread_status_message( String::ucompose ( _("Searching %1 partitions"), device_paths[ t ] ) ) ;
		if ( open_device_and_disk( device_paths[ t ], false ) )
		{
			temp_device .Reset() ;

			//device info..
			temp_device .add_path( device_paths[ t ] ) ;
			temp_device .add_paths( pp_info .get_alternate_paths( temp_device .get_path() ) ) ;

			temp_device .model 	=	lp_device ->model ;
			temp_device .length 	=	lp_device ->length ;
			temp_device .sector_size	=	lp_device ->sector_size ;
			temp_device .heads 	=	lp_device ->bios_geom .heads ;
			temp_device .sectors 	=	lp_device ->bios_geom .sectors ;
			temp_device .cylinders	=	lp_device ->bios_geom .cylinders ;
			temp_device .cylsize 	=	temp_device .heads * temp_device .sectors ; 
		
			//make sure cylsize is at least 1 MiB
			if ( temp_device .cylsize < (MEBIBYTE / temp_device .sector_size) )
				temp_device .cylsize = (MEBIBYTE / temp_device .sector_size) ;
				
			//normal harddisk
			if ( lp_disk )
			{
				temp_device .disktype =	lp_disk ->type ->name ;
				temp_device .max_prims = ped_disk_get_max_primary_partition_count( lp_disk ) ;
				
				set_device_partitions( temp_device ) ;
				set_mountpoints( temp_device .partitions ) ;
				set_used_sectors( temp_device .partitions ) ;
			
				if ( temp_device .highest_busy )
				{
					temp_device .readonly = ! commit_to_os( 1 ) ;
					//Clear libparted messages.  Typically these are:
					//  The kernel was unable to re-read the partition table...
					libparted_messages .clear() ;
				}
			}
			//harddisk without disklabel
			else
			{
				temp_device .disktype =
						/* TO TRANSLATORS:  unrecognized
						 * means that the partition table for this
						 * disk device is unknown or not recognized.
						 */
						_("unrecognized") ;
				temp_device .max_prims = -1 ;
				
				Partition partition_temp ;
				partition_temp .Set_Unallocated( temp_device .get_path(),
								 0,
								 temp_device .length,
								 temp_device .sector_size,
								 false );
				//Place libparted messages in this unallocated partition
				partition_temp .messages .insert( partition_temp .messages .end(),
								  libparted_messages. begin(),
								  libparted_messages .end() ) ;
				libparted_messages .clear() ;
				temp_device .partitions .push_back( partition_temp );
			}
					
			devices .push_back( temp_device ) ;
			
			close_device_and_disk() ;
		}
	}

	//clear leftover information...	
	//NOTE that we cannot clear mountinfo since it might be needed in get_all_mountpoints()
	set_thread_status_message("") ;
	fstab_info .clear() ;
}

// runs gpart on the specified parameter
void GParted_Core::guess_partition_table(const Device & device, Glib::ustring &buff)
{
	int pid, stdoutput, stderror;
	std::vector<std::string> argvproc, envpproc;
	gunichar tmp;

	//Get the char string of the sector_size
	std::ostringstream ssIn;
    ssIn << device.sector_size;
    Glib::ustring str_ssize = ssIn.str();

	//Build the command line
	argvproc.push_back("gpart");
	argvproc.push_back(device.get_path());
	argvproc.push_back("-s");
	argvproc.push_back(str_ssize);

	envpproc .push_back( "LC_ALL=C" ) ;
	envpproc .push_back( "PATH=" + Glib::getenv( "PATH" ) ) ;

	Glib::spawn_async_with_pipes(Glib::get_current_dir(), argvproc,
		envpproc, Glib::SPAWN_SEARCH_PATH, sigc::slot<void>(),
		&pid, NULL, &stdoutput, &stderror);

	this->iocOutput=Glib::IOChannel::create_from_fd(stdoutput);

	while(this->iocOutput->read(tmp)==Glib::IO_STATUS_NORMAL)
	{
		buff+=tmp;
	}
	this->iocOutput->close();

	return;
}

void GParted_Core::set_thread_status_message( Glib::ustring msg )
{
	//Remember to clear status message when finished with thread.
	thread_status_message = msg ;
}

Glib::ustring GParted_Core::get_thread_status_message( )
{
	return thread_status_message ;
}

bool GParted_Core::snap_to_cylinder( const Device & device, Partition & partition, Glib::ustring & error ) 
{
	Sector diff = 0;

	//Determine if partition size is less than half a disk cylinder
	bool less_than_half_cylinder = false;
	if ( ( partition .sector_end - partition .sector_start ) < ( device .cylsize / 2 ) )
		less_than_half_cylinder = true;

	if ( partition.type == TYPE_LOGICAL ||
	     partition.sector_start == device .sectors
	   )
	{
		//Must account the relative offset between:
		// (A) the Extended Boot Record sector and the next track of the
		//     logical partition (usually 63 sectors), and
		// (B) the Master Boot Record sector and the next track of the first
		//     primary partition
		diff = (partition .sector_start - device .sectors) % device .cylsize ;
	}
	else if ( partition.sector_start == 34 )
	{
		// (C) the GUID Partition Table (GPT) and the start of the data
		//     partition at sector 34
		diff = (partition .sector_start - 34 ) % device .cylsize ;
	}
	else
	{
		diff = partition .sector_start % device .cylsize ;
	}
	if ( diff && ! partition .strict_start  )
	{
		if ( diff < ( device .cylsize / 2 ) || less_than_half_cylinder )
			partition .sector_start -= diff ;
		else
			partition .sector_start += (device .cylsize - diff ) ;
	}

	diff = (partition .sector_end +1) % device .cylsize ;
	if ( diff )
	{
		if ( diff < ( device .cylsize / 2 ) && ! less_than_half_cylinder )
			partition .sector_end -= diff ;
		else
			partition .sector_end += (device .cylsize - diff ) ;
	}

	return true ;
}

bool GParted_Core::snap_to_mebibyte( const Device & device, Partition & partition, Glib::ustring & error ) 
{
	Sector diff = 0;
	if ( partition .sector_start < 2 || partition .type == TYPE_LOGICAL )
	{
		//Must account the relative offset between:
		// (A) the Master Boot Record sector and the first primary/extended partition, and
		// (B) the Extended Boot Record sector and the logical partition

		//If strict_start is set then do not adjust sector start.
		//If this partition is not simply queued for a reformat then
		//  add space minimum to force alignment to next mebibyte.
		if (   (! partition .strict_start)
		    && (partition .free_space_before == 0)
		    && ( partition .status != STAT_FORMATTED)
		   )
		{
			//Unless specifically told otherwise, the Linux kernel considers extended
			//  boot records to be two sectors long, in order to "leave room for LILO".
			partition .sector_start += 2 ;
		}
	}

	//Align start sector
	diff = Sector(partition .sector_start % ( MEBIBYTE / partition .sector_size ));
	if ( diff && (   (! partition .strict_start)
	              || (   partition .strict_start
	                  && (   partition .status == STAT_NEW
	                      || partition .status == STAT_COPY
	                     )
	                 )
	             )
	   )
		partition .sector_start += ( (MEBIBYTE / partition .sector_size) - diff) ;

	//If this is a logical partition not at end of drive then check to see if space is
	//  required for a following logical partition Extended Boot Record
	if ( partition .type == TYPE_LOGICAL )
	{
		//Locate the extended partition that contains the logical partitions.
		int index_extended = -1 ;
		for ( unsigned int t = 0 ; t < device .partitions .size() ; t++ )
		{
			if ( device .partitions[ t ] .type == TYPE_EXTENDED )
				index_extended = t ;
		}

		//If there is a following logical partition that starts a mebibyte or less
		//  from the end of this partition, then reserve a mebibyte for the EBR.
		if ( index_extended != -1 )
		{
			for ( unsigned int t = 0; t < device .partitions[ index_extended ] .logicals .size(); t++ )
			{
				if (   ( device .partitions[ index_extended ] .logicals[ t ] .type == TYPE_LOGICAL )
				    && ( device .partitions[ index_extended ] .logicals[ t ] .sector_start > partition .sector_end )
				    && ( ( device .partitions[ index_extended ] .logicals[ t ] .sector_start - partition .sector_end )
				         < ( MEBIBYTE / device .sector_size )
				       )
				   )
					partition .sector_end -= 1 ;
			}
		}
	}

	//If this is a GPT partition table then reserve a mebibyte at the end of the device
	//  for the backup partition table
	if (    device .disktype == "gpt" 
	    && ( ( device .length - partition .sector_end ) <= ( MEBIBYTE / device .sector_size ) )
	   )
	{
		partition .sector_end -= 1 ;
	}

	//Align end sector
	diff = (partition .sector_end + 1) % ( MEBIBYTE / partition .sector_size);
	if ( diff )
		partition .sector_end -= diff ;

	return true ;
}

bool GParted_Core::snap_to_alignment( const Device & device, Partition & partition, Glib::ustring & error )
{
	bool rc = true ;

	if ( partition .alignment == ALIGN_CYLINDER )
		rc = snap_to_cylinder( device, partition, error ) ;
	else if ( partition .alignment == ALIGN_MEBIBYTE )
		rc = snap_to_mebibyte( device, partition, error ) ;

	//Ensure that partition start and end are not beyond the ends of the disk device
	if ( partition .sector_start < 0 )
		partition .sector_start = 0 ;
	if ( partition .sector_end > device .length )
		partition .sector_end = device .length - 1 ;

	//do some basic checks on the partition
	if ( partition .get_sector_length() <= 0 )
	{
		error = String::ucompose(
				/* TO TRANSLATORS:  looks like   A partition cannot have a length of -1 sectors */
				_("A partition cannot have a length of %1 sectors"),
				partition .get_sector_length() ) ;
		return false ;
	}

	if ( partition .get_sector_length() < partition .sectors_used )
	{
		error = String::ucompose(
				/* TO TRANSLATORS: looks like   A partition with used sectors (2048) greater than its length (1536) is not valid */
				_("A partition with used sectors (%1) greater than its length (%2) is not valid"),
				partition .sectors_used,
				partition .get_sector_length() ) ;
		return false ;
	}

	//FIXME: it would be perfect if we could check for overlapping with adjacent partitions as well,
	//however, finding the adjacent partitions is not as easy as it seems and at this moment all the dialogs
	//already perform these checks. A perfect 'fixme-later' ;)

	return rc ;
}

bool GParted_Core::apply_operation_to_disk( Operation * operation )
{
	bool succes = false ;
	libparted_messages .clear() ;

	if ( calibrate_partition( operation ->partition_original, operation ->operation_detail ) )
		switch ( operation ->type )
		{	     
			case OPERATION_DELETE:
				succes = Delete( operation ->partition_original, operation ->operation_detail ) ;
				break ;
			case OPERATION_CHECK:
				succes = check_repair_filesystem( operation ->partition_original, operation ->operation_detail ) &&
					 maximize_filesystem( operation ->partition_original, operation ->operation_detail ) ;
				break ;
			case OPERATION_CREATE:
				succes = create( operation ->device, 
					         operation ->partition_new,
					         operation ->operation_detail ) ;
				break ;
			case OPERATION_RESIZE_MOVE:
				//in case the to be resized/moved partition was a 'copy of..', we need a real path...
				operation ->partition_new .add_path( operation ->partition_original .get_path(), true ) ;
				succes = resize_move( operation ->device,
					       	      operation ->partition_original,
						      operation ->partition_new,
						      operation ->operation_detail ) ;
				break ;
			case OPERATION_FORMAT:
				succes = format( operation ->partition_new, operation ->operation_detail ) ;
				break ;
			case OPERATION_COPY:
			//FIXME: in case of a new partition we should make sure the new partition is >= the source partition... 
			//i think it's best to do this in the dialog_paste
				succes = ( operation ->partition_original .type == TYPE_UNALLOCATED || 
					   calibrate_partition( operation ->partition_new, operation ->operation_detail ) ) &&
				
					 calibrate_partition( static_cast<OperationCopy*>( operation ) ->partition_copied,
							      operation ->operation_detail ) &&

					 copy( static_cast<OperationCopy*>( operation ) ->partition_copied,
					       operation ->partition_new,
					       static_cast<OperationCopy*>( operation ) ->partition_copied .get_byte_length(),
					       operation ->operation_detail ) ;
				break ;
			case OPERATION_LABEL_PARTITION:
				succes = label_partition( operation ->partition_new, operation ->operation_detail ) ;
				break ;
		}

	if ( libparted_messages .size() > 0 )
	{
		operation ->operation_detail .add_child( OperationDetail( _("libparted messages"), STATUS_INFO ) ) ;

		for ( unsigned int t = 0 ; t < libparted_messages .size() ; t++ )
			operation ->operation_detail .get_last_child() .add_child(
				OperationDetail( libparted_messages[ t ], STATUS_NONE, FONT_ITALIC ) ) ;
	}

	return succes ;
}

bool GParted_Core::set_disklabel( const Glib::ustring & device_path, const Glib::ustring & disklabel ) 
{
	bool return_value = false ;
	
	if ( open_device_and_disk( device_path, false ) )
	{
		PedDiskType *type = NULL ;
		type = ped_disk_type_get( disklabel .c_str() ) ;
		
		if ( type )
		{
			lp_disk = ped_disk_new_fresh( lp_device, type );
		
			return_value = commit() ;
		}
		
		close_device_and_disk() ;
	}

#ifndef USE_LIBPARTED_DMRAID
	//delete and recreate disk entries if dmraid
	DMRaid dmraid ;
	if ( return_value && dmraid .is_dmraid_device( device_path ) )
	{
		dmraid .purge_dev_map_entries( device_path ) ;
		dmraid .create_dev_map_entries( device_path ) ;
	}
#endif

	return return_value ;	
}
	
bool GParted_Core::toggle_flag( const Partition & partition, const Glib::ustring & flag, bool state ) 
{
	bool succes = false ;

	if ( open_device_and_disk( partition .device_path ) )
	{
		lp_partition = NULL ;
		if ( partition .type == GParted::TYPE_EXTENDED )
			lp_partition = ped_disk_extended_partition( lp_disk ) ;
		else
			lp_partition = ped_disk_get_partition_by_sector( lp_disk, partition .get_sector() ) ;
	
		if ( lp_partition )
		{
			PedPartitionFlag lp_flag = ped_partition_flag_get_by_name( flag .c_str() ) ;

			if ( lp_flag > 0 && ped_partition_set_flag( lp_partition, lp_flag, state ) )
				succes = commit() ;
		}
	
		close_device_and_disk() ;
	}

	return succes ;
}

const std::vector<FS> & GParted_Core::get_filesystems() const 
{
	return FILESYSTEMS ;
}

const FS & GParted_Core::get_fs( GParted::FILESYSTEM filesystem ) const 
{
	for ( unsigned int t = 0 ; t < FILESYSTEMS .size() ; t++ )
		if ( FILESYSTEMS[ t ] .filesystem == filesystem )
			return FILESYSTEMS[ t ] ;
	
	return FILESYSTEMS .back() ;
}

std::vector<Glib::ustring> GParted_Core::get_disklabeltypes() 
{
	std::vector<Glib::ustring> disklabeltypes ;
	
	//msdos should be first in the list
	disklabeltypes .push_back( "msdos" ) ;
	
	 PedDiskType *disk_type ;
	 for ( disk_type = ped_disk_type_get_next( NULL ) ; disk_type ; disk_type = ped_disk_type_get_next( disk_type ) ) 
		 if ( Glib::ustring( disk_type->name ) != "msdos" )
			disklabeltypes .push_back( disk_type->name ) ;

	 return disklabeltypes ;
}

std::vector<Glib::ustring> GParted_Core::get_all_mountpoints() 
{
	std::vector<Glib::ustring> mountpoints ;

	for ( iter_mp = mount_info .begin() ; iter_mp != mount_info .end() ; ++iter_mp )
		mountpoints .insert( mountpoints .end(), iter_mp ->second .begin(), iter_mp ->second .end() ) ;

	return mountpoints ;
}
	
std::map<Glib::ustring, bool> GParted_Core::get_available_flags( const Partition & partition ) 
{
	std::map<Glib::ustring, bool> flag_info ;

	if ( open_device_and_disk( partition .device_path ) )
	{
		lp_partition = NULL ;
		if ( partition .type == GParted::TYPE_EXTENDED )
			lp_partition = ped_disk_extended_partition( lp_disk ) ;
		else
			lp_partition = ped_disk_get_partition_by_sector( lp_disk, partition .get_sector() ) ;
	
		if ( lp_partition )
		{
			for ( unsigned int t = 0 ; t < flags .size() ; t++ )
				if ( ped_partition_is_flag_available( lp_partition, flags[ t ] ) )
					flag_info[ ped_partition_flag_get_name( flags[ t ] ) ] =
						ped_partition_get_flag( lp_partition, flags[ t ] ) ;
		}
	
		close_device_and_disk() ;
	}

	return flag_info ;
}
	
Glib::ustring GParted_Core::get_libparted_version() 
{
	return ped_get_version() ;
}

//private functions...

void GParted_Core::init_maps() 
{
	mount_info .clear() ;
	fstab_info .clear() ;

	read_mountpoints_from_file( "/proc/mounts", mount_info ) ;
	read_mountpoints_from_file_swaps( "/proc/swaps", mount_info ) ;
	read_mountpoints_from_file( "/etc/mtab", mount_info ) ;
	read_mountpoints_from_file( "/etc/fstab", fstab_info ) ;
	
	//sort the mount points and remove duplicates.. (no need to do this for fstab_info)
	for ( iter_mp = mount_info .begin() ; iter_mp != mount_info .end() ; ++iter_mp )
	{
		std::sort( iter_mp ->second .begin(), iter_mp ->second .end() ) ;
		
		iter_mp ->second .erase( 
				std::unique( iter_mp ->second .begin(), iter_mp ->second .end() ),
				iter_mp ->second .end() ) ;
	}
}

void GParted_Core::read_mountpoints_from_file(
	const Glib::ustring & filename,
	std::map< Glib::ustring, std::vector<Glib::ustring> > & map )
{
	FS_Info fs_info ;  //Use cache of file system information

	FILE* fp = setmntent( filename .c_str(), "r" ) ;

	if ( fp == NULL )
		return ;

	struct mntent* p = NULL ;

	while ( (p = getmntent(fp)) != NULL )
	{
		Glib::ustring node = p->mnt_fsname ;

		Glib::ustring uuid = Utils::regexp_label( node, "^UUID=(.*)" ) ;
		if ( ! uuid .empty() )
			node = fs_info .get_path_by_uuid( uuid ) ;

		Glib::ustring label = Utils::regexp_label( node, "^LABEL=(.*)" ) ;
		if ( ! label .empty() )
			node = fs_info .get_path_by_label( label ) ;

		if ( ! node .empty() )
		{
			Glib::ustring mountpoint = p->mnt_dir ;

			//Only add node path(s) if mount point exists
			if ( file_test( mountpoint, Glib::FILE_TEST_EXISTS ) )
			{
				map[ node ] .push_back( mountpoint ) ;

				//If node is a symbolic link (e.g., /dev/root)
				//  then find real path and add entry
				if ( file_test( node, Glib::FILE_TEST_IS_SYMLINK ) )
				{
					char c_str[4096+1] ;
					//FIXME: it seems realpath is very unsafe to use (manpage)...
					if ( realpath( node .c_str(), c_str ) != NULL )
						map[ c_str ] .push_back( mountpoint ) ;
				}
			}
		}
	}

	endmntent( fp ) ;
}

void GParted_Core::read_mountpoints_from_file_swaps(
	const Glib::ustring & filename,
	std::map< Glib::ustring, std::vector<Glib::ustring> > & map )
{
	std::string line ;
	std::string node ;
	
	std::ifstream file( filename .c_str() ) ;
	if ( file )
	{
		while ( getline( file, line ) )
		{
			node = Utils::regexp_label( line, "^(/[^ ]+)" ) ;
			if ( node .size() > 0 )
				map[ node ] .push_back( "" /* no mountpoint for swap */ ) ;
		}
		file .close() ;
	}
}

Glib::ustring GParted_Core::get_partition_path( PedPartition * lp_partition )
{
	char * lp_path;  //we have to free the result of ped_partition_get_path()
	Glib::ustring partition_path = "Partition path not found";

	lp_path = ped_partition_get_path(lp_partition);
	if ( lp_path != NULL )
	{
		partition_path = lp_path;
		free(lp_path);
	}

#ifndef USE_LIBPARTED_DMRAID
	//Ensure partition path name is compatible with dmraid
	DMRaid dmraid;   //Use cache of dmraid device information
	if (   dmraid .is_dmraid_supported()
	    && dmraid .is_dmraid_device( partition_path )
	   )
	{
		partition_path = dmraid .make_path_dmraid_compatible(partition_path);
	}
#endif

	return partition_path ;
}

void GParted_Core::set_device_partitions( Device & device ) 
{
	int EXT_INDEX = -1 ;
	Proc_Partitions_Info pp_info ; //Use cache of proc partitions information
	FS_Info fs_info ;  //Use cache of file system information
#ifndef USE_LIBPARTED_DMRAID
	DMRaid dmraid ;    //Use cache of dmraid device information
#endif

	//clear partitions
	device .partitions .clear() ;

	lp_partition = ped_disk_next_partition( lp_disk, NULL ) ;
	while ( lp_partition )
	{
		libparted_messages .clear() ;
		partition_temp .Reset() ;
		bool partition_is_busy = false ;

		//Retrieve partition path
		Glib::ustring partition_path = get_partition_path( lp_partition );

		switch ( lp_partition ->type )
		{
			case PED_PARTITION_NORMAL:
			case PED_PARTITION_LOGICAL:
#ifndef USE_LIBPARTED_DMRAID
				//Handle dmraid devices differently because the minor number might not
				//  match the last number of the partition filename as shown by "ls -l /dev/mapper"
				//  This mismatch causes incorrect identification of busy partitions in ped_partition_is_busy(). 
				if ( dmraid .is_dmraid_device( device .get_path() ) )
				{
					//Try device_name + partition_number
					iter_mp = mount_info .find( device .get_path() + Utils::num_to_str( lp_partition ->num ) ) ;
					if ( iter_mp != mount_info .end() )
						partition_is_busy = true ;
					//Try device_name + p + partition_number
					iter_mp = mount_info .find( device .get_path() + "p" + Utils::num_to_str( lp_partition ->num ) ) ;
					if ( iter_mp != mount_info .end() )
						partition_is_busy = true ;
				}
				else
#endif
					partition_is_busy = ped_partition_is_busy( lp_partition ) ;

				partition_temp .Set( device .get_path(),
						     partition_path,
						     lp_partition ->num,
						     lp_partition ->type == 0 ?	GParted::TYPE_PRIMARY : GParted::TYPE_LOGICAL,
						     get_filesystem(),
						     lp_partition ->geom .start,
						     lp_partition ->geom .end,
						     device .sector_size,
						     lp_partition ->type,
						     partition_is_busy ) ;

				partition_temp .add_paths( pp_info .get_alternate_paths( partition_temp .get_path() ) ) ;
				set_flags( partition_temp ) ;

				if ( partition_temp .busy && partition_temp .partition_number > device .highest_busy )
					device .highest_busy = partition_temp .partition_number ;

				break ;
			
			case PED_PARTITION_EXTENDED:
#ifndef USE_LIBPARTED_DMRAID
				//Handle dmraid devices differently because the minor number might not
				//  match the last number of the partition filename as shown by "ls -l /dev/mapper"
				//  This mismatch causes incorrect identification of busy partitions in ped_partition_is_busy(). 
				if ( dmraid .is_dmraid_device( device .get_path() ) )
				{
					for ( unsigned int k = 5; k < 255; k++ )
					{
						//Try device_name + [5 to 255]
						iter_mp = mount_info .find( device .get_path() + Utils::num_to_str( k ) ) ;
						if ( iter_mp != mount_info .end() )
							partition_is_busy = true ;
						//Try device_name + p + [5 to 255]
						iter_mp = mount_info .find( device .get_path() + "p" + Utils::num_to_str( k ) ) ;
						if ( iter_mp != mount_info .end() )
							partition_is_busy = true ;
					}
				}
				else
#endif
					partition_is_busy = ped_partition_is_busy( lp_partition ) ;

				partition_temp .Set( device .get_path(),
						     partition_path, 
						     lp_partition ->num,
						     GParted::TYPE_EXTENDED,
						     GParted::FS_EXTENDED,
						     lp_partition ->geom .start,
						     lp_partition ->geom .end,
						     device .sector_size,
						     false,
						     partition_is_busy ) ;

				partition_temp .add_paths( pp_info .get_alternate_paths( partition_temp .get_path() ) ) ;
				set_flags( partition_temp ) ;

				EXT_INDEX = device .partitions .size() ;
				break ;

			default:
				break;
		}

		//Avoid reading additional file system information if there is no path
		if ( partition_temp .get_path() != "" )
		{
			bool label_found = false ;
			partition_temp .label = fs_info .get_label( partition_temp .get_path(), label_found ) ;
			if ( ! label_found )
				read_label( partition_temp ) ;

			partition_temp .uuid = fs_info .get_uuid( partition_temp .get_path() ) ;
		}

		partition_temp .messages .insert( partition_temp .messages .end(),
						  libparted_messages. begin(),
						  libparted_messages .end() ) ;
		
		//if there's an end, there's a partition ;)
		if ( partition_temp .sector_end > -1 )
		{
			if ( ! partition_temp .inside_extended )
				device .partitions .push_back( partition_temp );
			else
				device .partitions[ EXT_INDEX ] .logicals .push_back( partition_temp ) ;
		}

		//next partition (if any)
		lp_partition = ped_disk_next_partition( lp_disk, lp_partition ) ;
	}

	if ( EXT_INDEX > -1 )
		insert_unallocated( device .get_path(),
				    device .partitions[ EXT_INDEX ] .logicals,
				    device .partitions[ EXT_INDEX ] .sector_start,
				    device .partitions[ EXT_INDEX ] .sector_end,
				    device .sector_size,
				    true ) ;

	insert_unallocated( device .get_path(), device .partitions, 0, device .length -1, device .sector_size, false ) ; 
}

GParted::FILESYSTEM GParted_Core::get_filesystem() 
{
	char magic1[16] = "";
	char magic2[16] = "";

	//Check for LUKS encryption prior to libparted file system detection.
	//  Otherwise encrypted file systems such as ext3 will be detected by
	//  libparted as 'ext3'.

	//LUKS encryption
	char * buf = static_cast<char *>( malloc( lp_device ->sector_size ) ) ;
	if ( buf )
	{
		ped_device_open( lp_device );
		ped_geometry_read( & lp_partition ->geom, buf, 0, 1 ) ;
		memcpy(magic1, buf+0, 6) ;  //set binary magic data
		ped_device_close( lp_device );
		free( buf ) ;

		if ( 0 == memcmp( magic1 , "LUKS\xBA\xBE", 6 ) )
		{
			temp = _( "Linux Unified Key Setup encryption is not yet supported." ) ;
			temp += "\n" ;
			partition_temp .messages .push_back( temp ) ;
			return GParted::FS_LUKS ;
		}
	}

	FS_Info fs_info ;
	Glib::ustring fs_type = "" ;

	//Standard libparted file system detection
	if ( lp_partition && lp_partition ->fs_type )
	{
		fs_type = lp_partition ->fs_type ->name ;

		//TODO:  Temporary code to detect ext4.
		//       Replace when libparted >= 1.9.0 is chosen as minimum required version.
		temp = fs_info .get_fs_type( get_partition_path( lp_partition ) ) ;
		if ( temp == "ext4" || temp == "ext4dev" )
			fs_type = temp ;
	}

	//FS_Info (blkid) file system detection because current libparted (v2.2) does not
	//  appear to detect file systems for sector sizes other than 512 bytes.
	if ( fs_type .empty() )
	{
		//TODO: blkid does not return anything for an "extended" partition.  Need to handle this somehow
		fs_type = fs_info.get_fs_type( get_partition_path( lp_partition ) ) ;
	}

	if ( ! fs_type .empty() )
	{
		if ( fs_type == "extended" )
			return GParted::FS_EXTENDED ;
		else if ( fs_type == "btrfs" )
			return GParted::FS_BTRFS ;
		else if ( fs_type == "ext2" )
			return GParted::FS_EXT2 ;
		else if ( fs_type == "ext3" )
			return GParted::FS_EXT3 ;
		else if ( fs_type == "ext4" ||
		          fs_type == "ext4dev" )
			return GParted::FS_EXT4 ;
		else if ( fs_type == "linux-swap" ||
		          fs_type == "linux-swap(v1)" ||
		          fs_type == "linux-swap(new)" ||
		          fs_type == "linux-swap(v0)" ||
		          fs_type == "linux-swap(old)" ||
		          fs_type == "swap" )
			return GParted::FS_LINUX_SWAP ;
		else if ( fs_type == "fat16" )
			return GParted::FS_FAT16 ;
		else if ( fs_type == "fat32" )
			return GParted::FS_FAT32 ;
		else if ( fs_type == "ntfs" )
			return GParted::FS_NTFS ;
		else if ( fs_type == "reiserfs" )
			return GParted::FS_REISERFS ;
		else if ( fs_type == "xfs" )
			return GParted::FS_XFS ;
		else if ( fs_type == "jfs" )
			return GParted::FS_JFS ;
		else if ( fs_type == "hfs" )
			return GParted::FS_HFS ;
		else if ( fs_type == "hfs+" ||
		          fs_type == "hfsplus" )
			return GParted::FS_HFSPLUS ;
		else if ( fs_type == "ufs" )
			return GParted::FS_UFS ;
	}


	//other file systems libparted couldn't detect (i've send patches for these file systems to the parted guys)
	// - no patches sent to parted for lvm2, or luks

	//reiser4
	buf = static_cast<char *>( malloc( lp_device ->sector_size ) ) ;
	if ( buf )
	{
		ped_device_open( lp_device );
		ped_geometry_read( & lp_partition ->geom
		                 , buf
		                 , (65536 / lp_device ->sector_size) 
		                 , 1
		                 ) ;
		memcpy(magic1, buf+0, 7) ; //set binary magic data
		ped_device_close( lp_device );
		free( buf ) ;

		if ( 0 == memcmp( magic1, "ReIsEr4", 7 ) )
			return GParted::FS_REISER4 ;
	}

	//lvm2
	//NOTE: lvm2 is not a file system but we do wish to recognize the Physical Volume
	buf = static_cast<char *>( malloc( lp_device ->sector_size ) ) ;
	if ( buf )
	{
		ped_device_open( lp_device );
		if ( lp_device ->sector_size == 512 )
		{
			ped_geometry_read( & lp_partition ->geom, buf, 1, 1 ) ;
			memcpy(magic1, buf+ 0, 8) ; // set binary magic data
			memcpy(magic2, buf+24, 4) ; // set binary magic data
		}
		else
		{
			ped_geometry_read( & lp_partition ->geom, buf, 0, 1 ) ;
			memcpy(magic1, buf+ 0+512, 8) ; // set binary magic data
			memcpy(magic2, buf+24+512, 4) ; // set binary magic data
		}
		ped_device_close( lp_device );
		free( buf ) ;

		if (    0 == memcmp( magic1, "LABELONE", 8 )
		     && 0 == memcmp( magic2, "LVM2", 4 ) )
		{
			temp = _( "Logical Volume Management is not yet supported." ) ;
			temp += "\n" ;
			partition_temp .messages .push_back( temp ) ;
			return GParted::FS_LVM2 ;
		}
	}

	//btrfs
	const Sector BTRFS_SUPER_INFO_SIZE   = 4096 ;
	const Sector BTRFS_SUPER_INFO_OFFSET = (64 * 1024) ;
	const char* const BTRFS_SIGNATURE  = "_BHRfS_M" ;

	char    buf_btrfs[BTRFS_SUPER_INFO_SIZE] ;

	ped_device_open( lp_device ) ;
	ped_geometry_read( & lp_partition ->geom
	                 , buf_btrfs
	                 , (BTRFS_SUPER_INFO_OFFSET / lp_device ->sector_size)
	                 , (BTRFS_SUPER_INFO_SIZE / lp_device ->sector_size)
	                 ) ;
	memcpy(magic1, buf_btrfs+64, strlen(BTRFS_SIGNATURE) ) ;  //set binary magic data
	ped_device_close( lp_device ) ;

	if ( 0 == memcmp( magic1, BTRFS_SIGNATURE, strlen(BTRFS_SIGNATURE) ) )
	{
		return GParted::FS_BTRFS ;
	}

	//no file system found....
	temp = _( "Unable to detect file system! Possible reasons are:" ) ;
	temp += "\n- "; 
	temp += _( "The file system is damaged" ) ;
	temp += "\n- " ; 
	temp += _( "The file system is unknown to GParted" ) ;
	temp += "\n- "; 
	temp += _( "There is no file system available (unformatted)" ) ; 
	temp += "\n- "; 
	/* TO TRANSLATORS: looks like  The device entry /dev/sda5 is missing */
	temp += String::ucompose( _( "The device entry %1 is missing" ), get_partition_path( lp_partition ) ) ;
	
	partition_temp .messages .push_back( temp ) ;
	
	return GParted::FS_UNKNOWN ;
}
	
void GParted_Core::read_label( Partition & partition )
{
	if ( partition .type != TYPE_EXTENDED )
	{
		switch( get_fs( partition .filesystem ) .read_label )
		{
			case FS::EXTERNAL:
				if ( set_proper_filesystem( partition .filesystem ) )
					p_filesystem ->read_label( partition ) ;
				break ;
			case FS::LIBPARTED:
				break ;

			default:
				break ;
		}
	}
}

void GParted_Core::insert_unallocated( const Glib::ustring & device_path,
				       std::vector<Partition> & partitions,
				       Sector start,
				       Sector end,
				       Byte_Value sector_size,
				       bool inside_extended )
{
	partition_temp .Reset() ;
	partition_temp .Set_Unallocated( device_path, 0, 0, sector_size, inside_extended ) ;
	
	//if there are no partitions at all..
	if ( partitions .empty() )
	{
		partition_temp .sector_start = start ;
		partition_temp .sector_end = end ;
		
		partitions .push_back( partition_temp );
		
		return ;
	}
		
	//start <---> first partition start
	if ( (partitions .front() .sector_start - start) > (MEBIBYTE / sector_size) )
	{
		partition_temp .sector_start = start ;
		partition_temp .sector_end = partitions .front() .sector_start -1 ;
		
		partitions .insert( partitions .begin(), partition_temp );
	}
	
	//look for gaps in between
	for ( unsigned int t =0 ; t < partitions .size() -1 ; t++ )
	{
		if (   ( ( partitions[ t + 1 ] .sector_start - partitions[ t ] .sector_end - 1 ) > (MEBIBYTE / sector_size) )
		    || (   ( partitions[ t + 1 ] .type != TYPE_LOGICAL )  // Only show exactly 1 MiB if following partition is not logical.
		        && ( ( partitions[ t + 1 ] .sector_start - partitions[ t ] .sector_end - 1 ) == (MEBIBYTE / sector_size) )
		       )
		   )
		{
			partition_temp .sector_start = partitions[ t ] .sector_end +1 ;
			partition_temp .sector_end = partitions[ t +1 ] .sector_start -1 ;

			partitions .insert( partitions .begin() + ++t, partition_temp );
		}
	}

	//last partition end <---> end
	if ( (end - partitions .back() .sector_end) >= (MEBIBYTE / sector_size) )
	{
		partition_temp .sector_start = partitions .back() .sector_end +1 ;
		partition_temp .sector_end = end ;
		
		partitions .push_back( partition_temp );
	}
}
	
void GParted_Core::set_mountpoints( std::vector<Partition> & partitions ) 
{
#ifndef USE_LIBPARTED_DMRAID
	DMRaid dmraid ;	//Use cache of dmraid device information
#endif
	for ( unsigned int t = 0 ; t < partitions .size() ; t++ )
	{
		if ( ( partitions[ t ] .type == GParted::TYPE_PRIMARY ||
		       partitions[ t ] .type == GParted::TYPE_LOGICAL
		     ) &&
		     partitions[ t ] .filesystem != GParted::FS_LINUX_SWAP &&
		     partitions[ t ] .filesystem != GParted::FS_LVM2 &&
		     partitions[ t ] .filesystem != GParted::FS_LUKS
		   )
		{
			if ( partitions[ t ] .busy )
			{
#ifndef USE_LIBPARTED_DMRAID
				//Handle dmraid devices differently because there may be more
				//  than one partition name.
				//  E.g., there might be names with and/or without a 'p' between
				//        the device name and partition number.
				if ( dmraid .is_dmraid_device( partitions[ t ] .device_path ) )
				{
					//Try device_name + partition_number
					iter_mp = mount_info .find( partitions[ t ] .device_path + Utils::num_to_str( partitions[ t ] .partition_number ) ) ;
					if ( iter_mp != mount_info .end() )
					{
						partitions[ t ] .add_mountpoints( iter_mp ->second ) ;
						break ;
					}
					//Try device_name + p + partition_number
					iter_mp = mount_info .find( partitions[ t ] .device_path + "p" + Utils::num_to_str( partitions[ t ] .partition_number ) ) ;
					if ( iter_mp != mount_info .end() )
					{
						partitions[ t ] .add_mountpoints( iter_mp ->second ) ;
						break ;
					}
				}
				else
				{
#endif
					//Normal device, not DMRaid device
					for ( unsigned int i = 0 ; i < partitions[ t ] .get_paths() .size() ; i++ )
					{
						iter_mp = mount_info .find( partitions[ t ] .get_paths()[ i ] ) ;
						if ( iter_mp != mount_info .end() )
						{
							partitions[ t ] .add_mountpoints( iter_mp ->second ) ;
							break ;
						}
					}
#ifndef USE_LIBPARTED_DMRAID
				}
#endif

				if ( partitions[ t ] .get_mountpoints() .empty() )
					partitions[ t ] .messages .push_back( _("Unable to find mount point") ) ;
			}
			else
			{
				iter_mp = fstab_info .find( partitions[ t ] .get_path() );
				if ( iter_mp != fstab_info .end() )
					partitions[ t ] .add_mountpoints( iter_mp ->second ) ;
			}
		}
		else if ( partitions[ t ] .type == GParted::TYPE_EXTENDED )
			set_mountpoints( partitions[ t ] .logicals ) ;
	}
}
	
void GParted_Core::set_used_sectors( std::vector<Partition> & partitions ) 
{
	struct statvfs sfs ; 

	for ( unsigned int t = 0 ; t < partitions .size() ; t++ )
	{
		if ( partitions[ t ] .filesystem != GParted::FS_LINUX_SWAP &&
		     partitions[ t ] .filesystem != GParted::FS_LUKS &&
		     partitions[ t ] .filesystem != GParted::FS_LVM2 &&
		     partitions[ t ] .filesystem != GParted::FS_UNKNOWN
		   )
		{
			if ( partitions[ t ] .type == GParted::TYPE_PRIMARY ||
			     partitions[ t ] .type == GParted::TYPE_LOGICAL ) 
			{
				if ( partitions[ t ] .busy )
				{
					if ( partitions[ t ] .get_mountpoints() .size() > 0  )
					{
						if ( statvfs( partitions[ t ] .get_mountpoint() .c_str(), &sfs ) == 0 )
							partitions[ t ] .Set_Unused( sfs .f_bfree * (sfs .f_bsize / partitions[ t ] .sector_size) ) ;
						else
							partitions[ t ] .messages .push_back( 
								"statvfs (" + 
								partitions[ t ] .get_mountpoint() + 
								"): " + 
								Glib::strerror( errno ) ) ;
					}
				}
				else
				{
					switch( get_fs( partitions[ t ] .filesystem ) .read )
					{
						case GParted::FS::EXTERNAL	:
							if ( set_proper_filesystem( partitions[ t ] .filesystem ) )
								p_filesystem ->set_used_sectors( partitions[ t ] ) ;
							break ;
						case GParted::FS::LIBPARTED	:
							LP_set_used_sectors( partitions[ t ] ) ;
							break ;

						default:
							break ;
					}
				}

				if ( partitions[ t ] .sectors_used == -1 )
				{
					temp = _("Unable to read the contents of this file system!") ;
					temp += "\n" ;
					temp += _("Because of this some operations may be unavailable.") ;
					if ( ! Utils::get_filesystem_software( partitions[ t ] .filesystem ) .empty() )
					{
						temp += "\n\n" ;
						temp += _( "The cause might be a missing software package.") ;
						temp += "\n" ;
						/*TO TRANSLATORS: looks like The following list of software packages is required for NTFS file system support:  ntfsprogs. */
						temp += String::ucompose( _("The following list of software packages is required for %1 file system support:  %2."),
						                          Utils::get_filesystem_string( partitions[ t ] .filesystem ),
						                          Utils::get_filesystem_software( partitions[ t ] .filesystem )
						                        ) ;
					}
					partitions[ t ] .messages .push_back( temp ) ;
				}
			}
			else if ( partitions[ t ] .type == GParted::TYPE_EXTENDED )
				set_used_sectors( partitions[ t ] .logicals ) ;
		}
	}
}

void GParted_Core::LP_set_used_sectors( Partition & partition )
{
	PedFileSystem *fs = NULL;
	PedConstraint *constraint = NULL;

	if ( lp_disk )
	{
		lp_partition = ped_disk_get_partition_by_sector( lp_disk, partition .get_sector() ) ;
		
		if ( lp_partition )
		{
			fs = ped_file_system_open( & lp_partition ->geom ); 	
					
			if ( fs )
			{
				constraint = ped_file_system_get_resize_constraint( fs ) ;
				if ( constraint )
				{
					partition .Set_Unused( partition .get_sector_length() - constraint ->min_size ) ;
					
					ped_constraint_destroy( constraint );
				}
												
				ped_file_system_close( fs ) ;
			}
		}
	}
}

void GParted_Core::set_flags( Partition & partition )
{
	for ( unsigned int t = 0 ; t < flags .size() ; t++ )
		if ( ped_partition_is_flag_available( lp_partition, flags[ t ] ) &&
		     ped_partition_get_flag( lp_partition, flags[ t ] ) )
			partition .flags .push_back( ped_partition_flag_get_name( flags[ t ] ) ) ;
}

bool GParted_Core::create( const Device & device, Partition & new_partition, OperationDetail & operationdetail ) 
{
	if ( new_partition .type == GParted::TYPE_EXTENDED )   
	{
		return create_partition( new_partition, operationdetail ) ;
	}
	else if ( create_partition( new_partition, operationdetail, (get_fs( new_partition .filesystem ) .MIN / new_partition .sector_size) ) )
	{
		if ( new_partition .filesystem == GParted::FS_UNFORMATTED )
			return true ;
		else
			return set_partition_type( new_partition, operationdetail ) &&
		       	       create_filesystem( new_partition, operationdetail ) ;
	}
	
	return false ;
}

bool GParted_Core::create_partition( Partition & new_partition, OperationDetail & operationdetail, Sector min_size )
{
	operationdetail .add_child( OperationDetail( _("create empty partition") ) ) ;
	
	new_partition .partition_number = 0 ;
		
	if ( open_device_and_disk( new_partition .device_path ) )
	{
		PedPartitionType type;
		lp_partition = NULL ;
		PedConstraint *constraint = NULL ;
		PedFileSystemType* fs_type = NULL ;
		
		//create new partition
		switch ( new_partition .type )
		{
			case GParted::TYPE_PRIMARY:
				type = PED_PARTITION_NORMAL ;
				break ;
			case GParted::TYPE_LOGICAL:
				type = PED_PARTITION_LOGICAL ;
				break ;
			case GParted::TYPE_EXTENDED:
				type = PED_PARTITION_EXTENDED ;
				break ;
				
			default	:
				type = PED_PARTITION_FREESPACE;
		}

		if ( new_partition .type != GParted::TYPE_EXTENDED )
			fs_type = ped_file_system_type_get( "ext2" ) ;

		lp_partition = ped_partition_new( lp_disk,
						  type,
						  fs_type,
						  new_partition .sector_start,
						  new_partition .sector_end ) ;
	
		if ( lp_partition )
		{
			if (   new_partition .alignment == ALIGN_STRICT
			    || new_partition .alignment == ALIGN_MEBIBYTE
			   )
			{
				PedGeometry *geom = ped_geometry_new( lp_device,
								      new_partition .sector_start,
								      new_partition .get_sector_length() ) ;

				if ( geom )
					constraint = ped_constraint_exact( geom ) ;
			}
			else
				constraint = ped_constraint_any( lp_device );
			
			if ( constraint )
			{
				if ( min_size > 0 )
					constraint ->min_size = min_size ;
		
				if ( ped_disk_add_partition( lp_disk, lp_partition, constraint ) && commit() )
				{
					Glib::ustring partition_path = get_partition_path( lp_partition ) ;
					new_partition .add_path( partition_path, true ) ;

					new_partition .partition_number = lp_partition ->num ;
					new_partition .sector_start = lp_partition ->geom .start ;
					new_partition .sector_end = lp_partition ->geom .end ;
					
					operationdetail .get_last_child() .add_child( OperationDetail( 
						String::ucompose( _("path: %1"), new_partition .get_path() ) + "\n" +
						String::ucompose( _("start: %1"), new_partition .sector_start ) + "\n" +
						String::ucompose( _("end: %1"), new_partition .sector_end ) + "\n" +
						String::ucompose( _("size: %1 (%2)"),
								new_partition .get_sector_length(),
								Utils::format_size( new_partition .get_sector_length(), new_partition .sector_size ) ),
						STATUS_NONE,
						FONT_ITALIC ) ) ;
				}
			
				ped_constraint_destroy( constraint );
			}
		}
				
		close_device_and_disk() ;
	}

	bool succes = new_partition .partition_number > 0 && erase_filesystem_signatures( new_partition ) ;

#ifndef USE_LIBPARTED_DMRAID
	//create dev map entries if dmraid
	DMRaid dmraid ;
	if ( succes && dmraid .is_dmraid_device( new_partition .device_path ) )
		succes = dmraid .create_dev_map_entries( new_partition, operationdetail .get_last_child() ) ;
#endif

	operationdetail .get_last_child() .set_status( succes ? STATUS_SUCCES : STATUS_ERROR ) ;

	return succes ;
}
	
bool GParted_Core::create_filesystem( const Partition & partition, OperationDetail & operationdetail ) 
{
	/*TO TRANSLATORS: looks like create new ext3 file system */ 
	operationdetail .add_child( OperationDetail( String::ucompose(
							_("create new %1 file system"),
							Utils::get_filesystem_string( partition .filesystem ) ) ) ) ;
	
	bool succes = false ;
	switch ( get_fs( partition .filesystem ) .create )
	{
		case GParted::FS::NONE:
			break ;
		case GParted::FS::GPARTED:
			break ;
		case GParted::FS::LIBPARTED:
			break ;
		case GParted::FS::EXTERNAL:
			succes = set_proper_filesystem( partition .filesystem ) &&
				 p_filesystem ->create( partition, operationdetail .get_last_child() ) ;

			break ;
	}

	operationdetail .get_last_child() .set_status(  succes ? STATUS_SUCCES : STATUS_ERROR ) ;
	return succes ;
}

bool GParted_Core::format( const Partition & partition, OperationDetail & operationdetail )
{	
	//remove all file system signatures...
	erase_filesystem_signatures( partition ) ;

	return set_partition_type( partition, operationdetail ) && create_filesystem( partition, operationdetail ) ;
}

bool GParted_Core::Delete( const Partition & partition, OperationDetail & operationdetail ) 
{
	operationdetail .add_child( OperationDetail( _("delete partition") ) ) ;

	bool succes = false ;
	if ( open_device_and_disk( partition .device_path ) )
	{
		if ( partition .type == TYPE_EXTENDED )
			lp_partition = ped_disk_extended_partition( lp_disk ) ;
		else
			lp_partition = ped_disk_get_partition_by_sector( lp_disk, partition .get_sector() ) ;
		
		succes = ped_disk_delete_partition( lp_disk, lp_partition ) && commit() ;
	
		close_device_and_disk() ;
	}

#ifndef USE_LIBPARTED_DMRAID
	//delete partition dev mapper entry, and delete and recreate all other affected dev mapper entries if dmraid
	DMRaid dmraid ;
	if ( succes && dmraid .is_dmraid_device( partition .device_path ) )
	{
		//Open disk handle before and close after to prevent application crash.
		if ( open_device_and_disk( partition .device_path ) )
		{
			if ( ! dmraid .delete_affected_dev_map_entries( partition, operationdetail .get_last_child() ) )
				succes = false ;	//comand failed

			if ( ! dmraid .create_dev_map_entries( partition, operationdetail .get_last_child() ) )
				succes = false ;	//command failed

			close_device_and_disk() ;
		}
	}
#endif

	operationdetail .get_last_child() .set_status( succes ? STATUS_SUCCES : STATUS_ERROR ) ;
	return succes ;
}

bool GParted_Core::label_partition( const Partition & partition, OperationDetail & operationdetail )	
{
	if( partition .label .empty() ) {
		operationdetail .add_child( OperationDetail( String::ucompose(
														_("Clear partition label on %1"),
														partition .get_path()
													 ) ) ) ;
	} else {
		operationdetail .add_child( OperationDetail( String::ucompose(
														_("Set partition label to \"%1\" on %2"),
														partition .label, partition .get_path()
													 ) ) ) ;
	}

	bool succes = false ;
	if ( partition .type != TYPE_EXTENDED )
	{
		switch( get_fs( partition .filesystem ) .write_label )
		{
			case FS::EXTERNAL:
				succes = set_proper_filesystem( partition .filesystem ) &&
					 p_filesystem ->write_label( partition, operationdetail .get_last_child() ) ;
				break ;
			case FS::LIBPARTED:
				break ;

			default:
				break ;
		}
	}

	operationdetail .get_last_child() .set_status( succes ? STATUS_SUCCES : STATUS_ERROR ) ;

	return succes ;	
}

bool GParted_Core::resize_move( const Device & device,
				const Partition & partition_old,
			  	Partition & partition_new,
			  	OperationDetail & operationdetail ) 
{
	if (   (partition_new .alignment == ALIGN_STRICT)
	    || (partition_new .alignment == ALIGN_MEBIBYTE)
	    || partition_new .strict_start
	    || calculate_exact_geom( partition_old, partition_new, operationdetail )
	   )
	{
		if ( partition_old .type == TYPE_EXTENDED )
			return resize_move_partition( partition_old, partition_new, operationdetail ) ;

		if ( partition_new .sector_start == partition_old .sector_start )
			return resize( partition_old, partition_new, operationdetail ) ;

		if ( partition_new .get_sector_length() == partition_old .get_sector_length() )
			return move( device, partition_old, partition_new, operationdetail ) ;

		Partition temp ;
		if ( partition_new .get_sector_length() > partition_old .get_sector_length() )
		{
			//first move, then grow. Since old.length < new.length and new.start is valid, temp is valid.
			temp = partition_new ;
			temp .sector_end = temp .sector_start + partition_old .get_sector_length() -1 ;
		}

		if ( partition_new .get_sector_length() < partition_old .get_sector_length() )
		{
			//first shrink, then move. Since new.length < old.length and old.start is valid, temp is valid.
			temp = partition_old ;
			temp .sector_end = partition_old .sector_start + partition_new .get_sector_length() -1 ;
		}

		PartitionAlignment previous_alignment = temp .alignment ;
		temp .alignment = ALIGN_STRICT ;
		bool succes = resize_move( device, partition_old, temp, operationdetail ) ;
		temp .alignment = previous_alignment ;

		return succes && resize_move( device, temp, partition_new, operationdetail ) ;
	}

	return false ;
}

bool GParted_Core::move( const Device & device,
			 const Partition & partition_old,
		   	 const Partition & partition_new,
		   	 OperationDetail & operationdetail ) 
{
	if ( partition_old .get_sector_length() != partition_new .get_sector_length() )
	{	
		operationdetail .add_child( OperationDetail(
				/* TO TRANSLATORS:  moving requires old and new length to be the same
				 * means that the length in bytes of the old partition and new partition
				 * must be the same.  If the sector sizes of the old partition and the
				 * new partition are the same, then the length in sectors must be the same.
				 */
				_("moving requires old and new length to be the same"), STATUS_ERROR, FONT_ITALIC ) ) ;

		return false ;
	}

	bool succes = false ;
	if ( check_repair_filesystem( partition_old, operationdetail ) )
	{
		//NOTE: Logical partitions are preceded by meta data.  To prevent this
		//      meta data from being overwritten we first expand the partition to
		//      encompass all of the space involved in the move.  In this way we
		//      prevent overwriting the meta data for this partition when we move
		//      this partition to the left.  We also prevent overwriting the meta
		//      data of a following partition when we move this partition to the
		//      right.
		Partition partition_all_space = partition_old ;
		partition_all_space .alignment = ALIGN_STRICT ;
		if ( partition_new .sector_start < partition_all_space. sector_start )
			partition_all_space .sector_start = partition_new. sector_start ;
		if ( partition_new .sector_end > partition_all_space.sector_end )
			partition_all_space .sector_end = partition_new. sector_end ;

		//Make old partition all encompassing and if move file system fails
		//  then return partition table to original state
		if ( resize_move_partition( partition_old, partition_all_space, operationdetail ) )
		{
			//Note move of file system is from old values to new values, not from
			//  the all encompassing values.
			if ( ! move_filesystem( partition_old, partition_new, operationdetail ) )
			{
				operationdetail .add_child( OperationDetail( _("rollback last change to the partition table") ) ) ;

				Partition partition_restore = partition_old ;
				partition_restore .alignment = ALIGN_STRICT ;  //Ensure that old partition boundaries are not modified
				if ( resize_move_partition( partition_all_space, partition_restore, operationdetail .get_last_child() ) )
					operationdetail .get_last_child() .set_status( STATUS_SUCCES ) ;
				else
					operationdetail .get_last_child() .set_status( STATUS_ERROR ) ;
			}
			else
				succes = true ;
		}

		//Make new partition from all encompassing partition
		succes =  succes && resize_move_partition( partition_all_space, partition_new, operationdetail ) ;

		succes = succes &&
			update_bootsector( partition_new, operationdetail ) &&
			check_repair_filesystem( partition_new, operationdetail ) &&
			maximize_filesystem( partition_new, operationdetail ) ;
	}

	return succes ;
}

bool GParted_Core::move_filesystem( const Partition & partition_old,
		   		    const Partition & partition_new,
				    OperationDetail & operationdetail ) 
{
	if ( partition_new .sector_start < partition_old .sector_start )
		operationdetail .add_child( OperationDetail( _("move file system to the left") ) ) ;
	else if ( partition_new .sector_start > partition_old .sector_start )
		operationdetail .add_child( OperationDetail( _("move file system to the right") ) ) ;
	else
	{
		operationdetail .add_child( OperationDetail( _("move file system") ) ) ;
		operationdetail .get_last_child() .add_child( 
			OperationDetail( _("new and old file system have the same position.  Hence skipping this operation"),
					 STATUS_NONE,
					 FONT_ITALIC ) ) ;

		operationdetail .get_last_child() .set_status( STATUS_SUCCES ) ;
		return true ;
	}

	bool succes = false ;
	switch ( get_fs( partition_old .filesystem ) .move )
	{
		case GParted::FS::NONE:
			break ;
		case GParted::FS::GPARTED:
			succes = false ;
			if ( partition_new .test_overlap( partition_old ) )
			{
				if ( copy_filesystem_simulation( partition_old, partition_new, operationdetail .get_last_child() ) )
				{
					operationdetail .get_last_child() .add_child( OperationDetail( _("perform real move") ) ) ;
					
					Sector total_done ;
					succes = copy_filesystem( partition_old,
								  partition_new,
								  operationdetail .get_last_child() .get_last_child(),
								  total_done ) ;
					
					operationdetail .get_last_child() .get_last_child() 
						.set_status( succes ? STATUS_SUCCES : STATUS_ERROR ) ;
					if ( ! succes )
					{
						rollback_transaction( partition_old,
								      partition_new,
								      operationdetail .get_last_child(),
								      total_done ) ;

						check_repair_filesystem( partition_old, operationdetail ) ;
					}
				}
			}
			else
				succes = copy_filesystem( partition_old, partition_new, operationdetail .get_last_child() ) ;

			break ;
		case GParted::FS::LIBPARTED:
			succes = resize_move_filesystem_using_libparted( partition_old,
									 partition_new,
								  	 operationdetail .get_last_child() ) ;
			break ;
		case GParted::FS::EXTERNAL:
			succes = set_proper_filesystem( partition_new .filesystem ) &&
			         p_filesystem ->move( partition_old
			                            , partition_new
			                            , operationdetail .get_last_child()
			                            ) ;
			break ;
	}

	operationdetail .get_last_child() .set_status( succes ? STATUS_SUCCES : STATUS_ERROR ) ;
	return succes ;
}

bool GParted_Core::resize_move_filesystem_using_libparted( const Partition & partition_old,
		  	      		            	   const Partition & partition_new,
						    	   OperationDetail & operationdetail ) 
{
	operationdetail .add_child( OperationDetail( _("using libparted"), STATUS_NONE ) ) ;

	bool return_value = false ;
	if ( open_device_and_disk( partition_old .device_path ) )
	{
		PedFileSystem *	fs = NULL ;
		PedGeometry * lp_geom = NULL ;	
		
		lp_geom = ped_geometry_new( lp_device,
					    partition_old .sector_start,
					    partition_old .get_sector_length() ) ;
		if ( lp_geom )
		{
			fs = ped_file_system_open( lp_geom );
			if ( fs )
			{
				lp_geom = NULL ;
				lp_geom = ped_geometry_new( lp_device,
							    partition_new .sector_start,
							    partition_new .get_sector_length() ) ;
				if ( lp_geom )
					return_value = ped_file_system_resize( fs, lp_geom, NULL ) && commit() ;

				ped_file_system_close( fs );
			}
		}

		close_device_and_disk() ;
	}

	return return_value ;
}

bool GParted_Core::resize( const Partition & partition_old, 
			   const Partition & partition_new,
			   OperationDetail & operationdetail )
{
	if ( partition_old .sector_start != partition_new .sector_start )
	{	
		operationdetail .add_child( OperationDetail( 
			_("resizing requires old and new start to be the same"), STATUS_ERROR, FONT_ITALIC ) ) ;

		return false ;
	}

	bool succes = false ;
	if ( check_repair_filesystem( partition_new, operationdetail ) )
	{
		succes = true ;

		if ( succes && partition_new .get_sector_length() < partition_old .get_sector_length() )
			succes = resize_filesystem( partition_old, partition_new, operationdetail ) ;
						
		if ( succes )
			succes = resize_move_partition( partition_old, partition_new, operationdetail ) ;
			
		//these 2 are always executed, however, if 1 of them fails the whole operation fails
		if ( ! check_repair_filesystem( partition_new, operationdetail ) )
			succes = false ;

		//expand file system to fit exactly in partition
		if ( ! maximize_filesystem( partition_new, operationdetail ) )
			succes = false ;
			
		return succes ;
	}
		
	return false ;
}

bool GParted_Core::resize_move_partition( const Partition & partition_old,
				     	  const Partition & partition_new,
					  OperationDetail & operationdetail )
{
	//i'm not too happy with this, but i think it is the correct way from a i18n POV
	enum Action
	{
		NONE			= 0,
		MOVE_RIGHT	 	= 1,
		MOVE_LEFT		= 2,
		GROW 			= 3,
		SHRINK			= 4,
		MOVE_RIGHT_GROW		= 5,
		MOVE_RIGHT_SHRINK	= 6,
		MOVE_LEFT_GROW		= 7,
		MOVE_LEFT_SHRINK	= 8
	} ;
	Action action = NONE ;

	if ( partition_new .get_sector_length() > partition_old .get_sector_length() )
		action = GROW ;
	else if ( partition_new .get_sector_length() < partition_old .get_sector_length() )
		action = SHRINK ;

	if ( partition_new .sector_start > partition_old .sector_start &&
	     partition_new .sector_end > partition_old .sector_end )
		action = action == GROW ? MOVE_RIGHT_GROW : action == SHRINK ? MOVE_RIGHT_SHRINK : MOVE_RIGHT ;
	else if ( partition_new .sector_start < partition_old .sector_start &&
	     partition_new .sector_end < partition_old .sector_end )
		action = action == GROW ? MOVE_LEFT_GROW : action == SHRINK ? MOVE_LEFT_SHRINK : MOVE_LEFT ;

	Glib::ustring description ;	
	switch ( action )
	{
		case NONE		:
			description = _("resize/move partition") ;
			break ;
		case MOVE_RIGHT		:
			description = _("move partition to the right") ;
			break ;
		case MOVE_LEFT		:
			description = _("move partition to the left") ;
			break ;
		case GROW 		:
			description = _("grow partition from %1 to %2") ;
			break ;
		case SHRINK		:
			description = _("shrink partition from %1 to %2") ;
			break ;
		case MOVE_RIGHT_GROW	:
			description = _("move partition to the right and grow it from %1 to %2") ;
			break ;
		case MOVE_RIGHT_SHRINK	:
			description = _("move partition to the right and shrink it from %1 to %2") ;
			break ;
		case MOVE_LEFT_GROW	:
			description = _("move partition to the left and grow it from %1 to %2") ;
			break ;
		case MOVE_LEFT_SHRINK	:
			description = _("move partition to the left and shrink it from %1 to %2") ;
			break ;
	}

	if ( ! description .empty() && action != NONE && action != MOVE_LEFT && action != MOVE_RIGHT )
		description = String::ucompose( description,
						Utils::format_size( partition_old .get_sector_length(), partition_old .sector_size ),
						Utils::format_size( partition_new .get_sector_length(), partition_new .sector_size ) ) ;

	operationdetail .add_child( OperationDetail( description ) ) ;

	
	if ( action == NONE )
	{
		operationdetail .get_last_child() .add_child( 
			OperationDetail( _("new and old partition have the same size and position.  Hence skipping this operation"),
					  STATUS_NONE,
					  FONT_ITALIC ) ) ;

		operationdetail .get_last_child() .set_status( STATUS_SUCCES ) ;
		return true ;
	}

	operationdetail .get_last_child() .add_child( 
		OperationDetail(
			String::ucompose( _("old start: %1"), partition_old .sector_start ) + "\n" +
			String::ucompose( _("old end: %1"), partition_old .sector_end ) + "\n" +
			String::ucompose( _("old size: %1 (%2)"),
					partition_old .get_sector_length(),
					Utils::format_size( partition_old .get_sector_length(), partition_old .sector_size ) ),
		STATUS_NONE, 
		FONT_ITALIC ) ) ;
	
	//finally the actual resize/move
	bool return_value = false ;
	
	PedConstraint *constraint = NULL ;
	lp_partition = NULL ;

	//sometimes the lp_partition ->geom .start,end and length values display random numbers
	//after going out of the 'if ( lp_partition)' scope. That's why we use some variables here.
	Sector new_start = -1, new_end = -1 ;
		
	if ( open_device_and_disk( partition_old .device_path ) )
	{
		if ( partition_old .type == GParted::TYPE_EXTENDED )
			lp_partition = ped_disk_extended_partition( lp_disk ) ;
		else		
			lp_partition = ped_disk_get_partition_by_sector( lp_disk, partition_old .get_sector() ) ;
		
		if ( lp_partition )
		{
			if (   (partition_new .alignment == ALIGN_STRICT)
			    || (partition_new .alignment == ALIGN_MEBIBYTE)
			    || partition_new .strict_start
			   ) {
				PedGeometry *geom = ped_geometry_new( lp_device,
									  partition_new .sector_start,
									  partition_new .get_sector_length() ) ;
				constraint = ped_constraint_exact( geom ) ;
			}
			else
				constraint = ped_constraint_any( lp_device ) ;

			if ( constraint )
			{
				if ( ped_disk_set_partition_geom( lp_disk,
								  lp_partition,
								  constraint,
								  partition_new .sector_start,
								  partition_new .sector_end ) )
				{
					new_start = lp_partition ->geom .start ;
					new_end = lp_partition ->geom .end ;

					return_value = commit() ;
				}
									
				ped_constraint_destroy( constraint );
			}
		}
		
		close_device_and_disk() ;
	}
	
	if ( return_value )
	{
		operationdetail .get_last_child() .add_child( 
			OperationDetail(
				String::ucompose( _("new start: %1"), new_start ) + "\n" +
				String::ucompose( _("new end: %1"), new_end ) + "\n" +
				String::ucompose( _("new size: %1 (%2)"),
						new_end - new_start + 1,
						Utils::format_size( new_end - new_start + 1, partition_new .sector_size ) ),
			STATUS_NONE, 
			FONT_ITALIC ) ) ;

#ifndef USE_LIBPARTED_DMRAID
		//update dev mapper entry if partition is dmraid.
		DMRaid dmraid ;
		if ( return_value && dmraid .is_dmraid_device( partition_new .device_path ) )
		{
			//Open disk handle before and close after to prevent application crash.
			if ( open_device_and_disk( partition_new .device_path ) )
			{
				return_value = dmraid .update_dev_map_entry( partition_new, operationdetail .get_last_child() ) ;
				close_device_and_disk() ;
			}
		}
#endif
	}
	
	operationdetail .get_last_child() .set_status( return_value ? STATUS_SUCCES : STATUS_ERROR ) ;
	
	return return_value ;
}

bool GParted_Core::resize_filesystem( const Partition & partition_old,
				      const Partition & partition_new,
				      OperationDetail & operationdetail,
				      bool fill_partition ) 
{
	//by default 'grow' to accomodate expand_filesystem()
	GParted::FS::Support action = get_fs( partition_old .filesystem ) .grow ;

	if ( ! fill_partition )
	{
		if ( partition_new .get_sector_length() < partition_old .get_sector_length() )
		{
			operationdetail .add_child( OperationDetail( _("shrink file system") ) ) ;
			action = get_fs( partition_old .filesystem ) .shrink ;
		}
		else if ( partition_new .get_sector_length() > partition_old .get_sector_length() )
			operationdetail .add_child( OperationDetail( _("grow file system") ) ) ;
		else
		{
			operationdetail .add_child( OperationDetail( _("resize file system") ) ) ;
			operationdetail .get_last_child() .add_child( 
				OperationDetail( 
					_("new and old file system have the same size.  Hence skipping this operation"),
					STATUS_NONE,
					FONT_ITALIC ) ) ;
		
			operationdetail .get_last_child() .set_status( STATUS_SUCCES ) ;
			return true ;
		}
	}

	bool succes = false ;
	switch ( action )
	{
		case GParted::FS::NONE:
			break ;
		case GParted::FS::GPARTED:
			break ;
		case GParted::FS::LIBPARTED:
			succes = resize_move_filesystem_using_libparted( partition_old,
									 partition_new,
								  	 operationdetail .get_last_child() ) ;
			break ;
		case GParted::FS::EXTERNAL:
			succes = set_proper_filesystem( partition_new .filesystem ) && 
				 p_filesystem ->resize( partition_new,
							operationdetail .get_last_child(), 
							fill_partition ) ;
			break ;
	}

	operationdetail .get_last_child() .set_status( succes ? STATUS_SUCCES : STATUS_ERROR ) ;
	return succes ;
}
	
bool GParted_Core::maximize_filesystem( const Partition & partition, OperationDetail & operationdetail ) 
{
	operationdetail .add_child( OperationDetail( _("grow file system to fill the partition") ) ) ;

	if ( get_fs( partition .filesystem ) .grow == GParted::FS::NONE )
	{
		operationdetail .get_last_child() .add_child( 
			OperationDetail( _("growing is not available for this file system"),
					  STATUS_NONE,
					  FONT_ITALIC ) ) ;

		operationdetail .get_last_child() .set_status( STATUS_N_A ) ;
		return true ;
	}
	
	return resize_filesystem( partition, partition, operationdetail, true ) ;
}

bool GParted_Core::copy( const Partition & partition_src,
			 Partition & partition_dst,
			 Byte_Value min_size,
			 OperationDetail & operationdetail ) 
{
	if ( partition_dst .get_byte_length() < partition_src .get_byte_length() )
	{	
		operationdetail .add_child( OperationDetail( 
			_("the destination is smaller than the source partition"), STATUS_ERROR, FONT_ITALIC ) ) ;

		return false ;
	}

	if ( check_repair_filesystem( partition_src, operationdetail ) )
	{
		bool succes = true ;
		if ( partition_dst .status == GParted::STAT_COPY )
		{
			/* Handle situation where src sector size is smaller than dst sector size and an additional partial dst sector is required. */
			succes = create_partition( partition_dst, operationdetail, ( (min_size + (partition_dst .sector_size - 1)) / partition_dst .sector_size ) ) ;
		}

		if ( succes && set_partition_type( partition_dst, operationdetail ) )
		{
			operationdetail .add_child( OperationDetail( 
				String::ucompose( _("copy file system of %1 to %2"),
						  partition_src .get_path(),
						  partition_dst .get_path() ) ) ) ;
						
			switch ( get_fs( partition_dst .filesystem ) .copy )
			{
				case GParted::FS::GPARTED :
						succes = copy_filesystem( partition_src,
									  partition_dst,
									  operationdetail .get_last_child() ) ;
						break ;
				
				case GParted::FS::LIBPARTED :
						//FIXME: see if copying through libparted has any advantages
						break ;

				case GParted::FS::EXTERNAL :
						succes = set_proper_filesystem( partition_dst .filesystem ) &&
							 p_filesystem ->copy( partition_src .get_path(),
								     	      partition_dst .get_path(),
									      operationdetail .get_last_child() ) ;
						break ;

				default :
						succes = false ;
						break ;
			}

			operationdetail .get_last_child() .set_status( succes ? STATUS_SUCCES : STATUS_ERROR ) ;

			return succes &&
			       update_bootsector( partition_dst, operationdetail ) &&
			       check_repair_filesystem( partition_dst, operationdetail ) &&
			       maximize_filesystem( partition_dst, operationdetail ) ;
		}
	}

	return false ;
}

bool GParted_Core::copy_filesystem_simulation( const Partition & partition_src,
				      	       const Partition & partition_dst,
			      		       OperationDetail & operationdetail ) 
{
	operationdetail .add_child( OperationDetail( _("perform read-only test") ) ) ;
	
	bool succes = copy_filesystem( partition_src, partition_dst, operationdetail .get_last_child(), true ) ;

	operationdetail .get_last_child() .set_status( succes ? STATUS_SUCCES : STATUS_ERROR ) ;
	return succes ;
}

bool GParted_Core::copy_filesystem( const Partition & partition_src,
				    const Partition & partition_dst,
				    OperationDetail & operationdetail,
				    bool readonly )
{
	Sector dummy ;
	return copy_filesystem( partition_src .device_path,
				partition_dst .device_path,
				partition_src .sector_start,
				partition_dst .sector_start,
				partition_src .sector_size,
				partition_dst .sector_size,
				partition_src .get_byte_length(),
				operationdetail,
				readonly,
				dummy ) ;
}

bool GParted_Core::copy_filesystem( const Partition & partition_src,
				    const Partition & partition_dst,
				    OperationDetail & operationdetail,
				    Byte_Value & total_done ) 
{
	return copy_filesystem( partition_src .device_path,
				partition_dst .device_path,
				partition_src .sector_start,
				partition_dst .sector_start,
				partition_src .sector_size,
				partition_dst .sector_size,
				partition_src .get_byte_length(),
				operationdetail,
				false,
				total_done ) ;
}
	
bool GParted_Core::copy_filesystem( const Glib::ustring & src_device,
				    const Glib::ustring & dst_device,
				    Sector src_start,
				    Sector dst_start,
				    Byte_Value src_sector_size,
				    Byte_Value dst_sector_size,
				    Byte_Value src_length,
				    OperationDetail & operationdetail,
				    bool readonly,
				    Byte_Value & total_done ) 
{
	operationdetail .add_child( OperationDetail( _("using internal algorithm"), STATUS_NONE ) ) ;
	operationdetail .add_child( OperationDetail( 
		String::ucompose( readonly ?
				/*TO TRANSLATORS: looks like  read 1.00 MiB */
				_("read %1") :
				/*TO TRANSLATORS: looks like  copy 1.00 MiB */
				_("copy %1"),
				Utils::format_size( src_length, 1 ) ),
				STATUS_NONE ) ) ;

	operationdetail .add_child( OperationDetail( _("finding optimal block size"), STATUS_NONE ) ) ;

	Byte_Value benchmark_blocksize = readonly ? (2 * MEBIBYTE) : (1 * MEBIBYTE), N = (16 * MEBIBYTE) ;
	Byte_Value optimal_blocksize = benchmark_blocksize ;
	Sector offset_read = src_start ;
	Sector offset_write = dst_start ;

	//Handle situation where we need to perform the copy beginning
	//  with the end of the partition and finishing with the start.
	if ( dst_start > src_start )
	{
		offset_read  += (src_length/src_sector_size) - (N/src_sector_size) ;
		/* Handle situation where src sector size is smaller than dst sector size and an additional partial dst sector is required. */
		offset_write += ((src_length + (dst_sector_size - 1))/dst_sector_size) - (N/dst_sector_size) ;
	}

	total_done = 0 ;
	Byte_Value done = 0 ;
	Glib::Timer timer ;
	double smallest_time = 1000000 ;
	bool succes = true ;

	//Benchmark copy times using different block sizes to determine optimal size
	while ( succes &&
		llabs( done ) + N <= src_length && 
		benchmark_blocksize <= N )
	{
		timer .reset() ;
		succes = copy_blocks( src_device, 
				      dst_device,
				      offset_read  + (done / src_sector_size),
				      offset_write + (done / dst_sector_size),
				      N,
				      benchmark_blocksize,
				      operationdetail .get_last_child(),
				      readonly,
				      total_done ) ;
		timer.stop() ;

		operationdetail .get_last_child() .get_last_child() .add_child( OperationDetail( 
			String::ucompose( _("%1 seconds"), timer .elapsed() ), STATUS_NONE, FONT_ITALIC ) ) ;

		if ( timer .elapsed() <= smallest_time )
		{
			smallest_time = timer .elapsed() ;
			optimal_blocksize = benchmark_blocksize ; 
		}
		benchmark_blocksize *= 2 ;

		if ( ( dst_start > src_start ) )
			done -= N ;
		else
			done += N ;
	}
	
	if ( succes )
		operationdetail .get_last_child() .add_child( OperationDetail( String::ucompose( 
				/*TO TRANSLATORS: looks like  optimal block size is 1.00 MiB */
				_("optimal block size is %1"),
				Utils::format_size( optimal_blocksize, 1 ) ),
				STATUS_NONE ) ) ;

	if ( succes && llabs( done ) < src_length )
		succes = copy_blocks( src_device, 
				    dst_device,
				    src_start + ( dst_start > src_start ? 0 : (done / src_sector_size) ),
				    dst_start + ( dst_start > src_start ? 0 : (done / dst_sector_size) ),
				    src_length - llabs( done ),
				    optimal_blocksize,
				    operationdetail,
				    readonly,
				    total_done ) ;

	operationdetail .add_child( OperationDetail( 
		String::ucompose( readonly ?
				/*TO TRANSLATORS: looks like  1.00 MiB (1048576 B) read */
				_("%1 (%2 B) read") :
				/*TO TRANSLATORS: looks like  1.00 MiB (1048576 B) copied */
				_("%1 (%2 B) copied"),
				Utils::format_size( total_done, 1 ), total_done ),
				STATUS_NONE ) ) ;
	return succes ;
}

void GParted_Core::rollback_transaction( const Partition & partition_src,
					 const Partition & partition_dst,
					 OperationDetail & operationdetail,
					 Byte_Value total_done )
{
	if ( total_done > 0 )
	{
		operationdetail .add_child( OperationDetail( _("roll back last transaction") ) ) ;

		//find out exactly which part of the file system was copied (and to where it was copied)..
		Partition temp_src = partition_src ;
		Partition temp_dst = partition_dst ;

		if ( partition_dst .sector_start > partition_src .sector_start )
		{
			temp_src .sector_start = temp_src .sector_end - ( (total_done / temp_src .sector_size) - 1 ) ;
			temp_dst .sector_start = temp_dst .sector_end - ( (total_done / temp_dst .sector_size) - 1 ) ;
		}
		else
		{
			temp_src .sector_end = temp_src .sector_start + ( (total_done / temp_src .sector_size) - 1 ) ;
			temp_dst .sector_end = temp_dst .sector_start + ( (total_done / temp_dst .sector_size) - 1 ) ;
		}

		//and copy it back (NOTE the reversed dst and src)
		bool succes = copy_filesystem( temp_dst, temp_src, operationdetail .get_last_child() ) ;

		operationdetail .get_last_child() .set_status( succes ? STATUS_SUCCES : STATUS_ERROR ) ;
	}
}

bool GParted_Core::check_repair_filesystem( const Partition & partition, OperationDetail & operationdetail ) 
{
	operationdetail .add_child( OperationDetail( 
				String::ucompose(
						/* TO TRANSLATORS: looks like   check file system on /dev/sda5 for errors and (if possible) fix them */
						_("check file system on %1 for errors and (if possible) fix them"),
						  partition .get_path() ) ) ) ;
	
	bool succes = false ;
	switch ( get_fs( partition .filesystem ) .check )
	{
		case GParted::FS::NONE:
			operationdetail .get_last_child() .add_child(
				OperationDetail( _("checking is not available for this file system"),
						 STATUS_NONE,
						 FONT_ITALIC ) ) ;

			operationdetail .get_last_child() .set_status( STATUS_N_A ) ;
			return true ;	

			break ;
		case GParted::FS::GPARTED:
			break ;
		case GParted::FS::LIBPARTED:
			break ;
		case GParted::FS::EXTERNAL:
			succes = set_proper_filesystem( partition .filesystem ) &&
				 p_filesystem ->check_repair( partition, operationdetail .get_last_child() ) ;

			break ;
	}

	operationdetail .get_last_child() .set_status( succes ? STATUS_SUCCES : STATUS_ERROR ) ;
	return succes ;
}

bool GParted_Core::set_partition_type( const Partition & partition, OperationDetail & operationdetail )
{
	operationdetail .add_child( OperationDetail(
				String::ucompose( _("set partition type on %1"), partition .get_path() ) ) ) ;
	
	bool return_value = false ;
	
	if ( open_device_and_disk( partition .device_path ) )
	{
		PedFileSystemType * fs_type = 
			ped_file_system_type_get( Utils::get_filesystem_string( partition .filesystem ) .c_str() ) ;

		//If not found, and FS is linux-swap, then try linux-swap(v1)
		if ( ! fs_type && Utils::get_filesystem_string( partition .filesystem ) == "linux-swap" )
			fs_type = ped_file_system_type_get( "linux-swap(v1)" ) ;

		//If not found, and FS is linux-swap, then try linux-swap(new)
		if ( ! fs_type && Utils::get_filesystem_string( partition .filesystem ) == "linux-swap" )
			fs_type = ped_file_system_type_get( "linux-swap(new)" ) ;

		//default is Linux (83)
		if ( ! fs_type )
			fs_type = ped_file_system_type_get( "ext2" ) ;

		if ( fs_type )
		{
			lp_partition = ped_disk_get_partition_by_sector( lp_disk, partition .get_sector() ) ;

			if ( lp_partition &&
			     ped_partition_set_system( lp_partition, fs_type ) && 
			     commit() )
			{
				operationdetail .get_last_child() .add_child( 
					OperationDetail( String::ucompose( _("new partition type: %1"),
									   lp_partition ->fs_type ->name ),
							 STATUS_NONE,
							 FONT_ITALIC ) ) ;

				return_value = true ;
			}
		}

		close_device_and_disk() ;
	}

	operationdetail .get_last_child() .set_status( return_value ? STATUS_SUCCES : STATUS_ERROR ) ;
	return return_value ;
}
	
void GParted_Core::set_progress_info( Byte_Value total,
				      Byte_Value done,
				      const Glib::Timer & timer,
				      OperationDetail & operationdetail,
				      bool readonly ) 
{
	operationdetail .fraction = done / static_cast<double>( total ) ;

	std::time_t time_remaining = Utils::round( (total - done) / ( done / timer .elapsed() ) ) ;

	operationdetail .progress_text = 
		String::ucompose( readonly ?
				/*TO TRANSLATORS: looks like  1.00 MiB of 16.00 MiB read (00:01:59 remaining) */
				_("%1 of %2 read (%3 remaining)") :
				/*TO TRANSLATORS: looks like  1.00 MiB of 16.00 MiB copied (00:01:59 remaining) */
				_("%1 of %2 copied (%3 remaining)"),
				  Utils::format_size( done, 1 ),
				  Utils::format_size( total,1 ),
				  Utils::format_time( time_remaining) ) ; 
			
	operationdetail .set_description( 
		String::ucompose( readonly ?
				/*TO TRANSLATORS: looks like  1.00 MiB of 16.00 MiB read */
				_("%1 of %2 read") :
				/*TO TRANSLATORS: looks like  1.00 MiB of 16.00 MiB copied */
				_("%1 of %2 copied"),
				Utils::format_size( done, 1 ), Utils::format_size( total, 1 ) ),
				FONT_ITALIC ) ;
}
	
bool GParted_Core::copy_blocks( const Glib::ustring & src_device,
				const Glib::ustring & dst_device,
				Sector src_start,
				Sector dst_start,
				Byte_Value length,
				Byte_Value blocksize,
				OperationDetail & operationdetail,
				bool readonly,
				Byte_Value & total_done ) 
{
	if ( blocksize > length )
		blocksize = length ;

	if ( readonly )
		operationdetail .add_child( OperationDetail( 
				/*TO TRANSLATORS: looks like  read 16.00 MiB using a block size of 1.00 MiB */
				String::ucompose( _("read %1 using a block size of %2"), Utils::format_size( length, 1 ),
					Utils::format_size( blocksize, 1 ) ) ) ) ;
	else
		operationdetail .add_child( OperationDetail( 
				/*TO TRANSLATORS: looks like  copy 16.00 MiB using a block size of 1.00 MiB */
				String::ucompose( _("copy %1 using a block size of %2"), Utils::format_size( length, 1 ),
					Utils::format_size( blocksize, 1 ) ) ) ) ;

	Byte_Value done = length % blocksize ; 

	bool succes = false ;
	PedDevice *lp_device_src = ped_device_get( src_device .c_str() );
	PedDevice *lp_device_dst = src_device != dst_device ? ped_device_get( dst_device .c_str() ) : lp_device_src ;

	if ( lp_device_src && lp_device_dst && ped_device_open( lp_device_src ) && ped_device_open( lp_device_dst ) )
	{
		Byte_Value src_sector_size = lp_device_src ->sector_size ;
		Byte_Value dst_sector_size = lp_device_dst ->sector_size ;

		//Handle situation where we need to perform the copy beginning
		//  with the end of the partition and finishing with the start.
		if ( dst_start > src_start )
		{
			blocksize -= 2*blocksize ;
			done -= 2*done ;
			src_start += ( (length / src_sector_size) - 1 ) ;
			/* Handle situation where src sector size is smaller than dst sector size and an additional partial dst sector is required. */
			dst_start += ( ((length + (dst_sector_size - 1))/ dst_sector_size) - 1 ) ;
		}

		Glib::ustring error_message ;
		buf = static_cast<char *>( malloc( llabs( blocksize ) ) ) ;
		if ( buf )
		{
			ped_device_sync( lp_device_dst ) ;

			succes = true ;
			if ( done != 0 )
				succes = copy_block( lp_device_src,
						lp_device_dst,
						src_start,
						dst_start, 
						done,
						error_message,
						readonly ) ;
			if ( ! succes )
				done = 0 ;

			//add an empty sub which we will constantly update in the loop
			operationdetail .get_last_child() .add_child( OperationDetail( "", STATUS_NONE ) ) ;

			Glib::Timer timer_progress_timeout, timer_total ;
			while( succes && llabs( done ) < length )
			{
				succes = copy_block( lp_device_src,
						     lp_device_dst,
						     src_start + (done / src_sector_size),
						     dst_start + (done / dst_sector_size),
						     blocksize,
						     error_message,
						     readonly ) ; 
				if ( succes )
					done += blocksize ;

				if ( timer_progress_timeout .elapsed() >= 0.5 )
				{
					set_progress_info( length,
							   llabs( done + blocksize ),
							   timer_total,
							   operationdetail .get_last_child() .get_last_child(),
							   readonly ) ;
			
					timer_progress_timeout .reset() ;
				}
			}
			//set progress bar current info on completion
			set_progress_info( length,
			                   llabs( done ),
			                   timer_total,
			                   operationdetail .get_last_child() .get_last_child(),
			                   readonly ) ;
			
			free( buf ) ;
		}
		else
			error_message = Glib::strerror( errno ) ;

		//reset fraction to -1 to make room for a new one (or a pulsebar)
		operationdetail .get_last_child() .get_last_child() .fraction = -1 ;

		//final description
		operationdetail .get_last_child() .get_last_child() .set_description( 
			String::ucompose( readonly ?
					/*TO TRANSLATORS: looks like  1.00 MiB of 16.00 MiB read */
					_("%1 of %2 read") :
					/*TO TRANSLATORS: looks like  1.00 MiB of 16.00 MiB copied */
					_("%1 of %2 copied"),
					Utils::format_size( llabs( done ), 1 ),
					Utils::format_size( length, 1 ) ),
					FONT_ITALIC ) ;
		
		if ( ! succes && ! error_message .empty() )
			operationdetail .get_last_child() .add_child( 
				OperationDetail( error_message, STATUS_NONE, FONT_ITALIC ) ) ;
		
		total_done += llabs( done ) ;
	
		//close and destroy the devices..
		ped_device_close( lp_device_src ) ;
		ped_device_destroy( lp_device_src ) ; 

		if ( src_device != dst_device )
		{
			ped_device_close( lp_device_dst ) ;
			ped_device_destroy( lp_device_dst ) ;
		}
	}

	operationdetail .get_last_child() .set_status( succes ? STATUS_SUCCES : STATUS_ERROR ) ;
	return succes ;
}

bool GParted_Core::copy_block( PedDevice * lp_device_src,
			       PedDevice * lp_device_dst,
			       Sector offset_src,
			       Sector offset_dst,
			       Byte_Value block_length,
			       Glib::ustring & error_message,
			       bool readonly ) 
{
	Byte_Value sector_size_src = lp_device_src ->sector_size ;
	Byte_Value sector_size_dst = lp_device_dst ->sector_size ;

	//Handle case where src and dst sector sizes are different.
	//    E.g.,  5 sectors x 512 bytes/sector = ??? 2048 byte sectors
	Sector num_blocks_src = (llabs(block_length) + (sector_size_src - 1) ) / sector_size_src ;
	Sector num_blocks_dst = (llabs(block_length) + (sector_size_dst - 1) ) / sector_size_dst ;

	//Handle situation where we are performing copy operation beginning
	//  with the end of the partition and finishing with the start.
	if ( block_length < 0 )
	{
		block_length = llabs( block_length ) ;
		offset_src -= ( (block_length / sector_size_src) - 1 ) ;
		/* Handle situation where src sector size is smaller than dst sector size and an additional partial dst sector is required. */
		offset_dst -= ( ( (block_length + (sector_size_dst - 1)) / sector_size_dst) - 1 ) ;
	}

	if ( block_length != 0 )
	{
		if ( ped_device_read( lp_device_src, buf, offset_src, num_blocks_src ) )
		{
			if ( readonly || ped_device_write( lp_device_dst, buf, offset_dst, num_blocks_dst ) )
				return true ;
			else
				error_message = String::ucompose( _("Error while writing block at sector %1"), offset_dst ) ;
		}
		else
			error_message = String::ucompose( _("Error while reading block at sector %1"), offset_src ) ;
	}

	return false ;
}

bool GParted_Core::calibrate_partition( Partition & partition, OperationDetail & operationdetail ) 
{
	if ( partition .type == TYPE_PRIMARY || partition .type == TYPE_LOGICAL || partition .type == TYPE_EXTENDED )
	{
		operationdetail .add_child( OperationDetail( String::ucompose( _("calibrate %1"), partition .get_path() ) ) ) ;
	
		bool succes = false ;
		if ( open_device_and_disk( partition .device_path ) )
		{	
			if ( partition .type == GParted::TYPE_EXTENDED )
				lp_partition = ped_disk_extended_partition( lp_disk ) ;
			else	
				lp_partition = ped_disk_get_partition_by_sector( lp_disk, partition .get_sector() ) ;
		
			if ( lp_partition )//FIXME: add check to see if lp_partition ->type matches partition .type..
			{
				partition .add_path( get_partition_path( lp_partition ) ) ;

				partition .sector_start = lp_partition ->geom .start ;
				partition .sector_end = lp_partition ->geom .end ;

				operationdetail .get_last_child() .add_child( 
					OperationDetail(
						String::ucompose( _("path: %1"), partition .get_path() ) + "\n" +
						String::ucompose( _("start: %1"), partition .sector_start ) + "\n" +
						String::ucompose( _("end: %1"), partition .sector_end ) + "\n" +
						String::ucompose( _("size: %1 (%2)"),
								partition .get_sector_length(),
								Utils::format_size( partition .get_sector_length(), partition .sector_size ) ),
					STATUS_NONE, 
					FONT_ITALIC ) ) ;
				succes = true ;
			}

			close_device_and_disk() ;
		}

		operationdetail .get_last_child() .set_status( succes ? STATUS_SUCCES : STATUS_ERROR ) ;
		return succes ;
	}
	else //nothing to calibrate...
		return true ;
}

bool GParted_Core::calculate_exact_geom( const Partition & partition_old,
			       	         Partition & partition_new,
				         OperationDetail & operationdetail ) 
{
	operationdetail .add_child( OperationDetail( 
		String::ucompose( _("calculate new size and position of %1"), partition_new .get_path() ) ) ) ;

	operationdetail .get_last_child() .add_child( 
		OperationDetail(
			String::ucompose( _("requested start: %1"), partition_new .sector_start ) + "\n" +
			String::ucompose( _("requested end: %1"), partition_new .sector_end ) + "\n" +
			String::ucompose( _("requested size: %1 (%2)"),
					partition_new .get_sector_length(),
					Utils::format_size( partition_new .get_sector_length(), partition_new .sector_size ) ),
		STATUS_NONE,
		FONT_ITALIC ) ) ;
	
	bool succes = false ;
	if ( open_device_and_disk( partition_old .device_path ) )
	{
		lp_partition = NULL ;

		if ( partition_old .type == GParted::TYPE_EXTENDED )
			lp_partition = ped_disk_extended_partition( lp_disk ) ;
		else		
			lp_partition = ped_disk_get_partition_by_sector( lp_disk, partition_old .get_sector() ) ;

		if ( lp_partition )
		{
			PedConstraint *constraint = NULL ;
			constraint = ped_constraint_any( lp_device ) ;
			
			if ( constraint )
			{
				//FIXME: if we insert a weird partitionnew geom here (e.g. start > end) 
				//ped_disk_set_partition_geom() will still return true (althoug an lp exception is written
				//to stdout.. see if this also affect create_partition and resize_move_partition
				//sended a patch to fix this to libparted list. will probably be in 1.7.2
				if ( ped_disk_set_partition_geom( lp_disk,
								  lp_partition,
								  constraint,
								  partition_new .sector_start,
								  partition_new .sector_end ) )
				{
					partition_new .sector_start = lp_partition ->geom .start ;
					partition_new .sector_end = lp_partition ->geom .end ;
					succes = true ;
				}
					
				ped_constraint_destroy( constraint );
			}
		}

		close_device_and_disk() ;	
	}

	if ( succes ) 
	{
		operationdetail .get_last_child() .add_child( 
			OperationDetail(
				String::ucompose( _("new start: %1"), partition_new .sector_start ) + "\n" +
				String::ucompose( _("new end: %1"), partition_new .sector_end ) + "\n" +
				String::ucompose( _("new size: %1 (%2)"),
						partition_new .get_sector_length(),
						Utils::format_size( partition_new .get_sector_length(), partition_new .sector_size ) ),
			STATUS_NONE,
			FONT_ITALIC ) ) ;

#ifndef USE_LIBPARTED_DMRAID
		//Update dev mapper entry if partition is dmraid.
		DMRaid dmraid ;
		if ( succes && dmraid .is_dmraid_device( partition_new .device_path ) )
		{
			//Open disk handle before and close after to prevent application crash.
			if ( open_device_and_disk( partition_new .device_path ) )
			{
				succes = dmraid .update_dev_map_entry( partition_new, operationdetail .get_last_child() ) ;
				close_device_and_disk() ;
			}
		}
#endif
	}

	operationdetail .get_last_child() .set_status( succes ? STATUS_SUCCES : STATUS_ERROR ) ;
	return succes ;
}

bool GParted_Core::set_proper_filesystem( const FILESYSTEM & filesystem )
{
	delete p_filesystem;
		
	switch( filesystem )
	{
		case FS_BTRFS		: p_filesystem = new btrfs() ;	 	break ;
		case FS_EXT2		: p_filesystem = new ext2() ;	 	break ;
		case FS_EXT3		: p_filesystem = new ext3() ; 		break ;
		case FS_EXT4		: p_filesystem = new ext4() ; 		break ;
		case FS_LINUX_SWAP	: p_filesystem = new linux_swap() ; 	break ;
		case FS_FAT16		: p_filesystem = new fat16() ; 		break ;
		case FS_FAT32		: p_filesystem = new fat32() ; 		break ;
		case FS_NTFS		: p_filesystem = new ntfs() ; 		break ;
		case FS_REISERFS	: p_filesystem = new reiserfs() ; 	break ;
		case FS_REISER4		: p_filesystem = new reiser4() ;	break ;
		case FS_XFS		: p_filesystem = new xfs() ; 		break ;
		case FS_JFS		: p_filesystem = new jfs() ; 		break ;
		case FS_HFS		: p_filesystem = new hfs() ; 		break ;
		case FS_HFSPLUS		: p_filesystem = new hfsplus() ; 	break ;
		case FS_UFS		: p_filesystem = new ufs() ;	 	break ;
		default			: p_filesystem = NULL ;
	}

	return p_filesystem ;
}
	
bool GParted_Core::erase_filesystem_signatures( const Partition & partition ) 
{
	bool return_value = false ;
	
	if ( open_device_and_disk( partition .device_path ) )
	{
		lp_partition = ped_disk_get_partition_by_sector( lp_disk, partition .get_sector() ) ;

		if ( lp_partition && ped_file_system_clobber( & lp_partition ->geom ) )
		{
			//file systems not yet supported by libparted
			if ( ped_device_open( lp_device ) )
			{
				//reiser4 stores "ReIsEr4" at sector 128 with a sector size of 512 bytes
				// FIXME writing block of partially uninitialized bytes (security/privacy)
				return_value = ped_geometry_write( & lp_partition ->geom, "0000000", (65536 / lp_device ->sector_size), 1 ) ;

				ped_device_close( lp_device ) ;
			}
		}
		
		close_device_and_disk() ;	
	}

	return return_value ;
}
	
bool GParted_Core::update_bootsector( const Partition & partition, OperationDetail & operationdetail ) 
{
	//only for ntfs atm...
	//FIXME: this should probably be done in the fs classes...
	if ( partition .filesystem == FS_NTFS )
	{
		//The NTFS file system stores a value in the boot record called the
		//  Number of Hidden Sectors.  This value must match the partition start
		//  sector number in order for Windows to boot from the file system.
		//  For more details, refer to the NTFS Volume Boot Record at:
		//  http://www.geocities.com/thestarman3/asm/mbr/NTFSBR.htm

		operationdetail .add_child( OperationDetail( 
				/*TO TRANSLATORS: update boot sector of ntfs file system on /dev/sdd1 */
				String::ucompose( _("update boot sector of %1 file system on %2"),
					  Utils::get_filesystem_string( partition .filesystem ),
					  partition .get_path() ) ) ) ;

		//convert start sector to hex string
		std::stringstream ss ;
		ss << std::hex << partition .sector_start ;
		Glib::ustring hex = ss .str() ;

		//fill with zeros and reverse...
		hex .insert( 0, 8 - hex .length(), '0' ) ;
		Glib::ustring reversed_hex ;
		for ( int t = 6 ; t >= 0 ; t -=2 )
			reversed_hex .append( hex .substr( t, 2 ) ) ;

		//convert reversed hex codes into ascii characters
		char buf[4] ;
		for ( unsigned int k = 0; (k < 4 && k < (reversed_hex .length() / 2)); k++ )
		{
			Glib::ustring tmp_hex = "0x" + reversed_hex .substr( k * 2, 2 ) ;
			buf[k] = (char)( std::strtol( tmp_hex .c_str(), NULL, 16 ) ) ;
		}

		//write new Number of Hidden Sectors value into NTFS boot sector at offset 0x1C
		Glib::ustring error_message = "" ;
		std::ofstream dev_file ;
		dev_file .open( partition .get_path() .c_str(), std::ios::out | std::ios::binary ) ;
		if ( dev_file .is_open() )
		{
			dev_file .seekp( 0x1C ) ;
			if ( dev_file .good() )
			{
				dev_file .write( buf, 4 ) ;
				if ( dev_file .bad() )
				{
					/*TO TRANSLATORS: looks like Error trying to write to boot sector in /dev/sdd1 */
					error_message = String::ucompose( _("Error trying to write to boot sector in %1"), partition .get_path() ) ;
				}
			}
			else
			{
				/*TO TRANSLATORS: looks like Error trying to seek to position 0x1C in /dev/sdd1 */
				error_message = String::ucompose( _("Error trying to seek to position 0x1c in %1"), partition .get_path() ) ;
			}
			dev_file .close( ) ;
		}
		else
		{
			/*TO TRANSLATORS: looks like Error trying to open /dev/sdd1 */
			error_message = String::ucompose( _("Error trying to open %1"), partition .get_path() ) ;
		}

		//append error messages if any found
		bool succes = true ;
		if ( ! error_message .empty() )
		{
			succes = false ;
			error_message += "\n" ;
			/*TO TRANSLATORS: looks like Failed to set the number of hidden sectors to 05ab4f00 in the ntfs boot record. */
			error_message += String::ucompose( _("Failed to set the number of hidden sectors to %1 in the NTFS boot record."), reversed_hex ) ;
			error_message += "\n" ;
			error_message += String::ucompose( _("You might try the following command to correct the problem:"), reversed_hex ) ;
			error_message += "\n" ;
			error_message += String::ucompose( "echo %1 | xxd -r -p | dd conv=notrunc of=%2 bs=1 seek=28", reversed_hex, partition .get_path() ) ;
			operationdetail .get_last_child() .add_child( OperationDetail( error_message, STATUS_NONE, FONT_ITALIC ) ) ;
		}

		operationdetail .get_last_child() .set_status( succes ? STATUS_SUCCES : STATUS_ERROR ) ;
		return succes ;
	}

	return true ;
}
	
bool GParted_Core::open_device( const Glib::ustring & device_path )
{
	lp_device = ped_device_get( device_path .c_str() );
	
	return lp_device ;
}
	
bool GParted_Core::open_device_and_disk( const Glib::ustring & device_path, bool strict ) 
{
	lp_device = NULL ;
	lp_disk = NULL ;

	if ( open_device( device_path ) )
	{
		lp_disk = ped_disk_new( lp_device );
	
		//if ! disk and writeable it's probably a HD without disklabel.
		//We return true here and deal with them in GParted_Core::get_devices
		if ( lp_disk || ( ! strict && ! lp_device ->read_only ) )
			return true ;
		
		close_device_and_disk() ;
	}

	return false ;
}

void GParted_Core::close_disk()
{
	if ( lp_disk )
		ped_disk_destroy( lp_disk ) ;
	
	lp_disk = NULL ;
}

void GParted_Core::close_device_and_disk()
{
	close_disk() ;

	if ( lp_device )
		ped_device_destroy( lp_device ) ;
		
	lp_device = NULL ;
}	

bool GParted_Core::commit() 
{
	bool succes = ped_disk_commit_to_dev( lp_disk ) ;
	
	succes = commit_to_os( 10 ) && succes ;

	return succes ;
}

bool GParted_Core::commit_to_os( std::time_t timeout ) 
{
	bool succes ;
#ifndef USE_LIBPARTED_DMRAID
	DMRaid dmraid ;
	if ( dmraid .is_dmraid_device( lp_disk ->dev ->path ) )
		succes = true ;
	else
	{
#endif
		succes = ped_disk_commit_to_os( lp_disk ) ;
#ifndef HAVE_LIBPARTED_2_2_0_PLUS
		//Work around to try to alleviate problems caused by
		//  bug #604298 - Failure to inform kernel of partition changes
		//  If not successful the first time, try one more time.
		if ( ! succes )
		{
			sleep( 1 ) ;
			succes = ped_disk_commit_to_os( lp_disk ) ;
		}
#endif
#ifndef USE_LIBPARTED_DMRAID
	}
#endif

	settle_device( timeout ) ;

	return succes ;
}

void GParted_Core::settle_device( std::time_t timeout )
{
	if ( ! Glib::find_program_in_path( "udevsettle" ) .empty() )
		Utils::execute_command( "udevsettle --timeout=" + Utils::num_to_str( timeout ) ) ;
	else if ( ! Glib::find_program_in_path( "udevadm" ) .empty() )
		Utils::execute_command( "udevadm settle --timeout=" + Utils::num_to_str( timeout ) ) ;
	else
		sleep( timeout ) ;
}

PedExceptionOption GParted_Core::ped_exception_handler( PedException * e ) 
{
        std::cout << e ->message << std::endl ;

        libparted_messages .push_back( e->message ) ;

	return PED_EXCEPTION_UNHANDLED ;
}

GParted_Core::~GParted_Core() 
{
	delete p_filesystem;
}
	
} //GParted

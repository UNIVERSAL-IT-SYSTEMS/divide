/* Copyright (C) 2008 Curtis Gedak
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
 
#include "../include/Dialog_Partition_Label.h"

namespace GParted
{

Dialog_Partition_Label::Dialog_Partition_Label( const Partition & partition )
{
	this ->partition = partition ;

	this ->set_resizable( false ) ;
	this ->set_has_separator( false ) ;
	this ->set_size_request( 300, 80 ) ;
	
	/*TO TRANSLATORS: dialog title, looks like Set partition label on /dev/hda3 */
	this ->set_title( String::ucompose( _("Set partition label on %1"), partition .get_path() ) );

	//Table
	int top = 0, bottom = 1 ;

	//Create table to hold Label and entry box
	table = manage( new Gtk::Table() ) ;
	table ->set_border_width( 5 ) ;
	table ->set_col_spacings(10 ) ;
	this ->get_vbox() ->pack_start( *table, Gtk::PACK_SHRINK ) ;
	//Label
	table ->attach( * Utils::mk_label( "<b>" + Glib::ustring( _("Label:") ) + "</b>" ),
			0, 1,
			top, bottom,
			Gtk::FILL ) ;
	//Create Text entry box
	entry = manage( new Gtk::Entry() );
	entry ->set_max_length( 30 );
	entry ->set_width_chars( 20 );
	entry ->set_activates_default( true );
	entry ->set_text( partition.label );
	entry ->select_region( 0, entry ->get_text_length() );
	//Add entry box to table
	table ->attach( *entry,
			1, 2,
			top++, bottom++,
			Gtk::FILL ) ;

	this ->add_button( Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL ) ;
	this ->add_button( Gtk::Stock::OK, Gtk::RESPONSE_OK ) ;
	this ->set_default_response( Gtk::RESPONSE_OK ) ;
	this ->show_all_children() ;
}

Dialog_Partition_Label::~Dialog_Partition_Label()
{
}

Glib::ustring Dialog_Partition_Label::get_new_label()
{
	return Utils::trim( Glib::ustring( entry ->get_text() ) );
}

} //GParted

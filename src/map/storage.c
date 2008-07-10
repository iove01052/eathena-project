// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/db.h"
#include "../common/nullpo.h"
#include "../common/malloc.h"
#include "../common/showmsg.h"

#include "map.h" // struct map_session_data
#include "storage.h"
#include "chrif.h"
#include "itemdb.h"
#include "clif.h"
#include "intif.h"
#include "pc.h"
#include "guild.h"
#include "battle.h"
#include "atcommand.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static DBMap* storage_db; // int account_id -> struct storage*
static DBMap* guild_storage_db; // int guild_id -> struct guild_storage*

/*==========================================
 * 倉庫内アイテムソート
 *------------------------------------------*/
int storage_comp_item(const void *_i1, const void *_i2)
{
	struct item *i1 = (struct item *)_i1;
	struct item *i2 = (struct item *)_i2;

	if (i1->nameid == i2->nameid)
		return 0;
	else if (!(i1->nameid) || !(i1->amount))
		return 1;
	else if (!(i2->nameid) || !(i2->amount))
		return -1;
	return i1->nameid - i2->nameid;
}
 
void storage_sortitem (struct storage_data *stor)
{
	nullpo_retv(stor);
	qsort(stor->storage_, MAX_STORAGE, sizeof(struct item), storage_comp_item);
}

void storage_gsortitem (struct guild_storage* gstor)
{
	nullpo_retv(gstor);
	qsort(gstor->storage_, MAX_GUILD_STORAGE, sizeof(struct item), storage_comp_item);
}

/*==========================================
 * 初期化とか
 *------------------------------------------*/
int do_init_storage(void) // map.c::do_init()から呼ばれる
{
	storage_db=idb_alloc(DB_OPT_RELEASE_DATA);
	guild_storage_db=idb_alloc(DB_OPT_RELEASE_DATA);
	return 1;
}
void do_final_storage(void) // by [MC Cameri]
{
	storage_db->destroy(storage_db,NULL);
	guild_storage_db->destroy(guild_storage_db,NULL);
}


static int storage_reconnect_sub(DBKey key,void *data,va_list ap)
{ //Parses storage and saves 'dirty' ones upon reconnect. [Skotlex]
	int type = va_arg(ap, int);
	if (type)
	{	//Guild Storage
		struct guild_storage* stor = (struct guild_storage*) data;
		if (stor->dirty && stor->storage_status == 0) //Save closed storages.
			storage_guild_storagesave(0, stor->guild_id,0);
	}
	else
	{	//Account Storage
		struct storage_data* stor = (struct storage_data*) data;
		if (stor->dirty && stor->storage_status == 0) //Save closed storages.
			storage_storage_save(stor->account_id, stor->dirty==2?1:0);
	}
	return 0;
}

//Function to be invoked upon server reconnection to char. To save all 'dirty' storages [Skotlex
void do_reconnect_storage(void)
{
	storage_db->foreach(storage_db, storage_reconnect_sub, 0);
	guild_storage_db->foreach(guild_storage_db, storage_reconnect_sub, 1);
}

static void* create_storage(DBKey key, va_list args)
{
	struct storage_data *stor;
	stor = (struct storage_data *) aCallocA (sizeof(struct storage_data), 1);
	stor->account_id = key.i;
	return stor;
}
struct storage_data *account2storage(int account_id)
{
	return (struct storage_data*)idb_ensure(storage_db,account_id,create_storage);
}

// Just to ask storage, without creation
struct storage_data *account2storage2(int account_id)
{
	return (struct storage_data*)idb_get(storage_db, account_id);
}

int storage_delete(int account_id)
{
	idb_remove(storage_db,account_id);
	return 0;
}

/*==========================================
 * Opens a storage. Returns:
 * 0 - success
 * 1 - fail
 * 2 - Storage requested from char-server (will open automatically later)
 *------------------------------------------*/
int storage_storageopen(struct map_session_data *sd)
{
	struct storage_data *stor;
	nullpo_retr(0, sd);

	if(sd->state.storage_flag)
		return 1; //Already open?
	
	if( !pc_can_give_items(pc_isGM(sd)) )
  	{ //check is this GM level is allowed to put items to storage
		clif_displaymessage(sd->fd, msg_txt(246));
		return 1;
	}
	
	if((stor = (struct storage_data*)idb_get(storage_db,sd->status.account_id)) == NULL)
  	{	//Request storage.
		intif_request_storage(sd->status.account_id);
		return 2;
	}
  
	if (stor->storage_status)
  		return 1; //Already open/player already has it open...

	stor->storage_status = 1;
	sd->state.storage_flag = 1;
	clif_storagelist(sd,stor);
	clif_updatestorageamount(sd,stor);
	return 0;
}

// helper function
int compare_item(struct item *a, struct item *b)
{
	if (a->nameid == b->nameid &&
		a->identify == b->identify &&
		a->refine == b->refine &&
		a->attribute == b->attribute)
	{
		int i;
		for (i = 0; i < MAX_SLOTS && (a->card[i] == b->card[i]); i++);
		return (i == MAX_SLOTS);
	}
	return 0;
}

/*==========================================
 * Internal add-item function.
 *------------------------------------------*/
static int storage_additem(struct map_session_data *sd,struct storage_data *stor,struct item *item_data,int amount)
{
	struct item_data *data;
	int i;

	if(item_data->nameid <= 0 || amount <= 0)
		return 1;
	
	data = itemdb_search(item_data->nameid);

	if (!itemdb_canstore(item_data, pc_isGM(sd)))
	{	//Check if item is storable. [Skotlex]
		clif_displaymessage (sd->fd, msg_txt(264));
		return 1;
	}
	
	if(itemdb_isstackable2(data)){ //Stackable
		for(i=0;i<MAX_STORAGE;i++){
			if( compare_item (&stor->storage_[i], item_data)) {
				if(amount > MAX_AMOUNT - stor->storage_[i].amount)
					return 1;
				stor->storage_[i].amount+=amount;
				clif_storageitemadded(sd,stor,i,amount);
				stor->dirty = 1;
				if(log_config.enable_logs&0x800)
					log_pick_pc(sd, "R", item_data->nameid, -amount, item_data);
				return 0;
			}
		}
	}
	//Add item
	for(i=0;i<MAX_STORAGE && stor->storage_[i].nameid;i++);
	
	if(i>=MAX_STORAGE)
		return 1;

	memcpy(&stor->storage_[i],item_data,sizeof(stor->storage_[0]));
	stor->storage_[i].amount=amount;
	stor->storage_amount++;
	clif_storageitemadded(sd,stor,i,amount);
	clif_updatestorageamount(sd,stor);
	stor->dirty = 1;
	if(log_config.enable_logs&0x800)
		log_pick_pc(sd, "R", item_data->nameid, -amount, item_data);
	return 0;
}
/*==========================================
 * Internal del-item function
 *------------------------------------------*/
static int storage_delitem(struct map_session_data *sd,struct storage_data *stor,int n,int amount)
{

	if(stor->storage_[n].nameid==0 || stor->storage_[n].amount<amount)
		return 1;

	stor->storage_[n].amount-=amount;
	if(log_config.enable_logs&0x800)
		log_pick_pc(sd, "R", stor->storage_[n].nameid, amount, &stor->storage_[n]);
	if(stor->storage_[n].amount==0){
		memset(&stor->storage_[n],0,sizeof(stor->storage_[0]));
		stor->storage_amount--;
		clif_updatestorageamount(sd,stor);
	}
	clif_storageitemremoved(sd,n,amount);

	stor->dirty = 1;
	return 0;
}
/*==========================================
 * Add an item to the storage from the inventory.
 *------------------------------------------*/
int storage_storageadd(struct map_session_data *sd,int index,int amount)
{
	struct storage_data *stor;

	nullpo_retr(0, sd);
	nullpo_retr(0, stor=account2storage2(sd->status.account_id));

	if((stor->storage_amount > MAX_STORAGE) || !stor->storage_status)
		return 0; // storage full / storage closed

	if(index<0 || index>=MAX_INVENTORY)
		return 0;

	if(sd->status.inventory[index].nameid <= 0)
		return 0; //No item on that spot
	
	if(amount < 1 || amount > sd->status.inventory[index].amount)
  		return 0;

//	log_tostorage(sd, index, 0);
	if(storage_additem(sd,stor,&sd->status.inventory[index],amount)==0)
  // remove item from inventory
		pc_delitem(sd,index,amount,0);

	return 1;
}

/*==========================================
 * Retrieve an item from the storage.
 *------------------------------------------*/
int storage_storageget(struct map_session_data *sd,int index,int amount)
{
	struct storage_data *stor;
	int flag;

	nullpo_retr(0, sd);
	nullpo_retr(0, stor=account2storage2(sd->status.account_id));

	
	if(index<0 || index>=MAX_STORAGE)
		return 0;

	if(stor->storage_[index].nameid <= 0)
		return 0; //Nothing there
	
	if(amount < 1 || amount > stor->storage_[index].amount)
		return 0;

	if((flag = pc_additem(sd,&stor->storage_[index],amount)) == 0)
		storage_delitem(sd,stor,index,amount);
	else
		clif_additem(sd,0,0,flag);
//	log_fromstorage(sd, index, 0);
	return 1;
}
/*==========================================
 * Move an item from cart to storage.
 *------------------------------------------*/
int storage_storageaddfromcart(struct map_session_data *sd,int index,int amount)
{
	struct storage_data *stor;

	nullpo_retr(0, sd);
	nullpo_retr(0, stor=account2storage2(sd->status.account_id));

	if(stor->storage_amount > MAX_STORAGE || !stor->storage_status)
  		return 0; // storage full / storage closed

	if(index< 0 || index>=MAX_CART)
  		return 0;

	if(sd->status.cart[index].nameid <= 0)
		return 0; //No item there.
	
	if(amount < 1 || amount > sd->status.cart[index].amount)
		return 0;

	if(storage_additem(sd,stor,&sd->status.cart[index],amount)==0)
		pc_cart_delitem(sd,index,amount,0);

	return 1;
}

/*==========================================
 * Get from Storage to the Cart
 *------------------------------------------*/
int storage_storagegettocart(struct map_session_data *sd,int index,int amount)
{
	struct storage_data *stor;

	nullpo_retr(0, sd);
	nullpo_retr(0, stor=account2storage2(sd->status.account_id));

	if(!stor->storage_status)
		return 0;
 
	if(index< 0 || index>=MAX_STORAGE)
		return 0;
	
	if(stor->storage_[index].nameid <= 0)
		return 0; //Nothing there.
	
	if(amount < 1 || amount > stor->storage_[index].amount)
		return 0;
	
	if(pc_cart_additem(sd,&stor->storage_[index],amount)==0)
		storage_delitem(sd,stor,index,amount);

	return 1;
}


/*==========================================
 * Modified By Valaris to save upon closing [massdriller]
 *------------------------------------------*/
int storage_storageclose(struct map_session_data *sd)
{
	struct storage_data *stor;

	nullpo_retr(0, sd);
	nullpo_retr(0, stor=account2storage2(sd->status.account_id));

	clif_storageclose(sd);
	if (stor->storage_status)
  	{
		if (save_settings&4)
			chrif_save(sd,0); //Invokes the storage saving as well.
		else
			storage_storage_save(sd->status.account_id, 0);
	}
	stor->storage_status=0;
	sd->state.storage_flag=0;
	return 0;
}

/*==========================================
 * When quitting the game.
 *------------------------------------------*/
int storage_storage_quit(struct map_session_data *sd, int flag)
{
	struct storage_data *stor;

	nullpo_retr(0, sd);
	nullpo_retr(0, stor=account2storage2(sd->status.account_id));
	
	if (stor->storage_status)
	{
		if (save_settings&4)
			chrif_save(sd, flag); //Invokes the storage saving as well.
		else
			storage_storage_save(sd->status.account_id, flag);
	}
	stor->storage_status = 0;
	sd->state.storage_flag = 0;
	return 0;
}

void storage_storage_dirty(struct map_session_data *sd)
{
	struct storage_data *stor;

	stor=account2storage2(sd->status.account_id);

	if(stor)
		stor->dirty = 1;
}

int storage_storage_save(int account_id, int final)
{
	struct storage_data *stor;

	stor=account2storage2(account_id);
	if(!stor) return 0;

	if(stor->dirty)
	{
		if (final) {
			stor->dirty = 2;
			stor->storage_status = 0; //To prevent further manipulation of it.
		}
		intif_send_storage(stor);
		return 1;
	}
	if (final) 
	{	//Clear storage from memory. Nothing to save.
		storage_delete(account_id);
		return 1;
	}

	return 0;
}

//Ack from Char-server indicating the storage was saved. [Skotlex]
int storage_storage_saved(int account_id)
{
	struct storage_data *stor;
	
	if((stor=account2storage2(account_id)) == NULL)
		return 0;

	if (stor->dirty == 2)
	{	//Final save of storage. Remove from memory.
	  	storage_delete(account_id);
		return 1;
	}

	if (stor->dirty && stor->storage_status == 0)
	{	//Only mark it clean if it's not in use. [Skotlex]
		stor->dirty = 0;
		storage_sortitem(stor);
		return 1;
	}
	return 0;
}

static void* create_guildstorage(DBKey key, va_list args)
{
	struct guild_storage *gs = NULL;
	gs = (struct guild_storage *) aCallocA(sizeof(struct guild_storage), 1);
	gs->guild_id=key.i;
	return gs;
}
struct guild_storage *guild2storage(int guild_id)
{
	struct guild_storage *gs = NULL;
	if(guild_search(guild_id) != NULL)
		gs=(struct guild_storage *) idb_ensure(guild_storage_db,guild_id,create_guildstorage);
	return gs;
}

struct guild_storage *guild2storage2(int guild_id)
{	//For just locating a storage without creating one. [Skotlex]
	return (struct guild_storage*)idb_get(guild_storage_db,guild_id);
}

int guild_storage_delete(int guild_id)	
{
	idb_remove(guild_storage_db,guild_id);
	return 0;
}

int storage_guild_storageopen(struct map_session_data *sd)
{
	struct guild_storage *gstor;

	nullpo_retr(0, sd);

	if(sd->status.guild_id <= 0)
		return 2;

	if(sd->state.storage_flag)
		return 1; //Can't open both storages at a time.
	
	if( !pc_can_give_items(pc_isGM(sd)) ) { //check is this GM level can open guild storage and store items [Lupus]
		clif_displaymessage(sd->fd, msg_txt(246));
		return 1;
	}

	if((gstor = guild2storage2(sd->status.guild_id)) == NULL) {
		intif_request_guild_storage(sd->status.account_id,sd->status.guild_id);
		return 0;
	}
	if(gstor->storage_status)
		return 1;
	
	gstor->storage_status = 1;
	sd->state.storage_flag = 2;
	clif_guildstoragelist(sd,gstor);
	clif_updateguildstorageamount(sd,gstor);
	return 0;
}

int guild_storage_additem(struct map_session_data *sd,struct guild_storage *stor,struct item *item_data,int amount)
{
	struct item_data *data;
	int i;

	nullpo_retr(1, sd);
	nullpo_retr(1, stor);
	nullpo_retr(1, item_data);
	nullpo_retr(1, data = itemdb_search(item_data->nameid));

	if(item_data->nameid <= 0 || amount <= 0)
		return 1;

	if (!itemdb_canguildstore(item_data, pc_isGM(sd)))
	{	//Check if item is storable. [Skotlex]
		clif_displaymessage (sd->fd, msg_txt(264));
		return 1;
	}

	if(itemdb_isstackable2(data)){ //Stackable
		for(i=0;i<MAX_GUILD_STORAGE;i++){
			if(compare_item(&stor->storage_[i], item_data)) {
				if(stor->storage_[i].amount+amount > MAX_AMOUNT)
					return 1;
				stor->storage_[i].amount+=amount;
				clif_guildstorageitemadded(sd,stor,i,amount);
				stor->dirty = 1;
				if(log_config.enable_logs&0x1000)
					log_pick_pc(sd, "G", item_data->nameid, -amount, item_data);
				return 0;
			}
		}
	}
	//Add item
	for(i=0;i<MAX_GUILD_STORAGE && stor->storage_[i].nameid;i++);
	
	if(i>=MAX_GUILD_STORAGE)
		return 1;
	
	memcpy(&stor->storage_[i],item_data,sizeof(stor->storage_[0]));
	stor->storage_[i].amount=amount;
	stor->storage_amount++;
	clif_guildstorageitemadded(sd,stor,i,amount);
	clif_updateguildstorageamount(sd,stor);
	stor->dirty = 1;
	if(log_config.enable_logs&0x1000)
		log_pick_pc(sd, "G", item_data->nameid, -amount, item_data);
	return 0;
}

int guild_storage_delitem(struct map_session_data *sd,struct guild_storage *stor,int n,int amount)
{
	nullpo_retr(1, sd);
	nullpo_retr(1, stor);

	if(stor->storage_[n].nameid==0 || stor->storage_[n].amount<amount)
		return 1;

	stor->storage_[n].amount-=amount;
	if(log_config.enable_logs&0x1000)
		log_pick_pc(sd, "G", stor->storage_[n].nameid, amount, &stor->storage_[n]);
	if(stor->storage_[n].amount==0){
		memset(&stor->storage_[n],0,sizeof(stor->storage_[0]));
		stor->storage_amount--;
		clif_updateguildstorageamount(sd,stor);
	}
	clif_storageitemremoved(sd,n,amount);
	stor->dirty = 1;
	return 0;
}

int storage_guild_storageadd(struct map_session_data *sd,int index,int amount)
{
	struct guild_storage *stor;

	nullpo_retr(0, sd);
	nullpo_retr(0, stor=guild2storage2(sd->status.guild_id));
		
	if (!stor->storage_status || stor->storage_amount > MAX_GUILD_STORAGE)
		return 0;
	
	if(index<0 || index>=MAX_INVENTORY)
		return 0;

	if(sd->status.inventory[index].nameid <= 0)
		return 0;
	
	if(amount < 1 || amount > sd->status.inventory[index].amount)
		return 0;

//	log_tostorage(sd, index, 1);
	if(guild_storage_additem(sd,stor,&sd->status.inventory[index],amount)==0)
		pc_delitem(sd,index,amount,0);

	return 1;
}

int storage_guild_storageget(struct map_session_data *sd,int index,int amount)
{
	struct guild_storage *stor;
	int flag;

	nullpo_retr(0, sd);
	nullpo_retr(0, stor=guild2storage2(sd->status.guild_id));

	if(!stor->storage_status)
  		return 0;
	
	if(index<0 || index>=MAX_GUILD_STORAGE)
		return 0;

	if(stor->storage_[index].nameid <= 0)
		return 0;
	
	if(amount < 1 || amount > stor->storage_[index].amount)
	  	return 0;

	if((flag = pc_additem(sd,&stor->storage_[index],amount)) == 0)
		guild_storage_delitem(sd,stor,index,amount);
	else
		clif_additem(sd,0,0,flag);
//	log_fromstorage(sd, index, 1);

	return 0;
}

int storage_guild_storageaddfromcart(struct map_session_data *sd,int index,int amount)
{
	struct guild_storage *stor;

	nullpo_retr(0, sd);
	nullpo_retr(0, stor=guild2storage2(sd->status.guild_id));

	if(!stor->storage_status || stor->storage_amount > MAX_GUILD_STORAGE)
		return 0;

	if(index<0 || index>=MAX_CART)
		return 0;

	if(sd->status.cart[index].nameid <= 0)
		return 0;
	
	if(amount < 1 || amount > sd->status.cart[index].amount)
		return 0;

	if(guild_storage_additem(sd,stor,&sd->status.cart[index],amount)==0)
		pc_cart_delitem(sd,index,amount,0);

	return 1;
}

int storage_guild_storagegettocart(struct map_session_data *sd,int index,int amount)
{
	struct guild_storage *stor;

	nullpo_retr(0, sd);
	nullpo_retr(0, stor=guild2storage2(sd->status.guild_id));

	if(!stor->storage_status)
	  	return 0;

	if(index<0 || index>=MAX_GUILD_STORAGE)
	  	return 0;
	
	if(stor->storage_[index].nameid<=0)
		return 0;
	
	if(amount < 1 || amount > stor->storage_[index].amount)
		return 0;

	if(pc_cart_additem(sd,&stor->storage_[index],amount)==0)
		guild_storage_delitem(sd,stor,index,amount);

	return 1;
}

int storage_guild_storagesave(int account_id, int guild_id, int flag)
{
	struct guild_storage *stor = guild2storage2(guild_id);

	if(stor)
	{
		if (flag) //Char quitting, close it.
			stor->storage_status = 0;
	 	if (stor->dirty)
			intif_send_guild_storage(account_id,stor);
		return 1;
	}
	return 0;
}

int storage_guild_storagesaved(int guild_id)
{
	struct guild_storage *stor;

	if((stor=guild2storage2(guild_id)) != NULL) {
		if (stor->dirty && stor->storage_status == 0)
		{	//Storage has been correctly saved.
			stor->dirty = 0;
			storage_gsortitem(stor);
		}
		return 1;
	}
	return 0;
}

int storage_guild_storageclose(struct map_session_data *sd)
{
	struct guild_storage *stor;

	nullpo_retr(0, sd);
	nullpo_retr(0, stor=guild2storage2(sd->status.guild_id));

	clif_storageclose(sd);
	if (stor->storage_status)
	{
		if (save_settings&4)
			chrif_save(sd, 0); //This one also saves the storage. [Skotlex]
		else
			storage_guild_storagesave(sd->status.account_id, sd->status.guild_id,0);
		stor->storage_status=0;
	}
	sd->state.storage_flag = 0;

	return 0;
}

int storage_guild_storage_quit(struct map_session_data *sd,int flag)
{
	struct guild_storage *stor;

	nullpo_retr(0, sd);
	nullpo_retr(0, stor=guild2storage2(sd->status.guild_id));
	
	if(flag)
	{	//Only during a guild break flag is 1 (don't save storage)
		sd->state.storage_flag = 0;
		stor->storage_status = 0;
		clif_storageclose(sd);
		if (save_settings&4)
			chrif_save(sd,0);
		return 0;
	}

	if(stor->storage_status) {
		if (save_settings&4)
			chrif_save(sd,0);
		else
			storage_guild_storagesave(sd->status.account_id,sd->status.guild_id,1);
	}
	sd->state.storage_flag = 0;
	stor->storage_status = 0;

	return 0;
}

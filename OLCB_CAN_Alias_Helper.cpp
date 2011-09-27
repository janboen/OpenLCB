#include "OLCB_CAN_Link.h"

void OLCB_CAN_Alias_Helper::initialize(OLCB_CAN_Link *link)
{
	_link = link;
	_helper_value = millis();
}

void OLCB_CAN_Alias_Helper::checkMessage(OLCB_CAN_Buffer *msg)
{
	//Serial.println("Checking message!");
	//while allocating aliases, we must restart the process if we receive an incoming message with an alias that matches the one being allocated
	//notice that we have to guard against attempting to allocate two identical aliases
	//notice too that we must ignore any CID that we have sent for the nodeID that generated it. Effectively, we can simply ignore any CIDs, and let those aliases that successfully generate a RID to impede the allocation of an identical alias earlier in the process.
	
	//first, check to see if the source alias matches anything in our queues.
	uint16_t alias = msg->getSourceAlias();
	//Serial.print("Checking against alias ");
	//Serial.println(alias, DEC);

	for(uint8_t i = 0; i < CAN_ALIAS_BUFFER_SIZE; ++i)
	{
		//TODO if an already allocated alias is duplicated elsewhere, we need
		//to reallocate it. No problem, but we also need to inform any vnodes using that alias that they are inhibited, and should not do anything! TODO!
		if(alias == _nodes[i].alias) //we only care if the aliases match
		{
//			Serial.print("Alias helper caught a match :( for alias ");
//			Serial.println(_nodes[i].alias, DEC);
			switch(_nodes[i].state)
			{
				case  ALIAS_HOLDING_STATE:
					//TODO this is no longer correct. We can and should defend ourselves here.
					_nodes[i].state = ALIAS_EMPTY_STATE; //fall through
				case  ALIAS_EMPTY_STATE:
					_nodes[i].alias = 0; //very simple, just make sure we don't accidentally use the same alias again.
					//Serial.println("Found a match with node:");
					//_nodes[i].node->print();
					break;
				case  ALIAS_CID1_STATE:
					//Serial.println("CID1 Found a match with node:");
					//_nodes[i].node->print();

					reAllocateAlias(&(_nodes[i])); //just start over, easy peasy
					break;
				case  ALIAS_RID_STATE:
					//ignore any CID messages, might be our own. The other node will reset when we emit an RID anyway, and if it was this node's CID, then we are properly ignoring it
					if(!msg->isCID())
					{
						//Serial.println("RID Found a match with node:");
						//_nodes[i].node->print();

						reAllocateAlias(&(_nodes[i]));
					}
					break;
				case ALIAS_CID4_STATE:
					//ignore any CID1,2,3 messages, might be our own
					if(!msg->isCID() || (msg->getVariableField()&0x7000) <=0x4000) //match with CID4
					{
					//Serial.println("CID4 Found a match with node:");
					//_nodes[i].node->print();

						reAllocateAlias(&(_nodes[i]));
					}
					break;

				case  ALIAS_CID3_STATE:
					//ignore any CID1,2 messages, might be our own
					if(!msg->isCID() || (msg->getVariableField()&0x7000) <=0x5000) //match with CID3 or 4
					{
					//Serial.println("CID3 Found a match with node:");
					//_nodes[i].node->print();

						reAllocateAlias(&(_nodes[i]));
					}
					break;

				case  ALIAS_CID2_STATE:
					//ignore any CID1 messages, might be our own
					if(!msg->isCID() || (msg->getVariableField()&0x7000) <=0x6000) //match with CID2, 3, or 4
					{
					//Serial.println("CID2 Found a match with node:");
					//_nodes[i].node->print();

						reAllocateAlias(&(_nodes[i]));
					}
					break;

				case  ALIAS_RELEASING_STATE:
					break; //do nothing, we're giving up the alias anyway
				case  ALIAS_AMD_STATE:
				case  ALIAS_READY_STATE:
					//here's where things get tricky. A vnode sending a lot of messages will trigger this case many many times. We need to determine whether the message was generated by our own VNode, or by a different node. So, we only pay attention to verifiednode messages (we can't ask for one very time, though, or we'll swamp our own vnode).
					//on the other hand, if the message is a CID, we need to nip it in the bud!
					if(msg->isCID())
					{
//						Serial.println("READY: heard CID, sending RID");
						_nodes[i].state = ALIAS_RESENDRID_STATE;
					}
					if(msg->isRID())
					{
						//send a VerifiedNID message as a particularly firm reminder that the offending node is in the wrong here, as the usual RID will be ignored.
						_nodes[i].state = ALIAS_SENDVERIFIEDNID_STATE;
						//_link->sendVerifiedID(_nodes[i].node);
					}
					else if(msg->isVerifiedNID())
					{
						OLCB_NodeID n;
						msg->getNodeID(&n);
						//Notice that we do not load up the alias! We already know the aliases are the same, and the comparator will only compare the alises if both are loaded. At this point, we want to know if the actual NIDs themselves are identical, and this part of the comparation is only triggered if one or both alises is '0', undefined.
						//n.alias = msg->getSourceAlias();
//						Serial.println("READY: heard verifiedNID");
//						Serial.println("*&*&*&");
						bool thesame = (n == *(_nodes[i].node));
//						if(thesame)
//							Serial.println("NID MATCHES! (YAY!)");
//						else
//							Serial.println("NID DOES NOT MATCH! (BOO!)");

						if(!thesame)
						{
							//uh-oh! same alias, different NID (notice that different alias, same NID == duplicate NID!!! TODO
//							n.print();
//							_nodes[i].node->print();
//							Serial.println("*&*&*&");
							//TODO This is where we have to put the owning vnode into an inhibited state while we reallocate its alias!
//							Serial.println("READY: reallocating");

							reAllocateAlias(&(_nodes[i]));
						}
//						else
//						{
//							Serial.println("READY: relief!");
//						}
					}
					break;
			}
		}
	}
}

void OLCB_CAN_Alias_Helper::update(void)
{
	//TODO is this really encessary?
	if(_nodes[index].node->alias == 0)
	{
		_nodes[index].state = ALIAS_EMPTY_STATE;
	}
	if(_nodes[index].state != ALIAS_EMPTY_STATE && _nodes[index].state != ALIAS_READY_STATE)
	{
		//Serial.print("update: index=");
		//Serial.println(index,DEC);
		//Serial.print("  state=");
		//Serial.println(_nodes[index].state, DEC);
	}
		
	//check queue for NIDS ready to send NID.
	switch(_nodes[index].state)
	{
			case ALIAS_EMPTY_STATE:
			case ALIAS_HOLDING_STATE:
			case ALIAS_READY_STATE:
				break;	//do nothing! move on to the next node
			case ALIAS_RELEASING_STATE: //emit AMR, return to initial state
				_nodes[index].state = ALIAS_HOLDING_STATE;
				if(!_link->sendAMR(_nodes[index].node))
				{
					_nodes[index].state = ALIAS_RELEASING_STATE;
				}
				break;
			case ALIAS_CID1_STATE:
				//send CID1!
				//Serial.println("sending CID1");
				//_nodes[index].node->print();
				_nodes[index].state = ALIAS_CID2_STATE;
				if(!_link->sendCID(_nodes[index].node, 1))
				{
					_nodes[index].state = ALIAS_CID1_STATE; 
				}
				break;
			case ALIAS_CID2_STATE:
				//send CID2!
				//Serial.println("sending CID2");
				//_nodes[index].node->print();
				_nodes[index].state = ALIAS_CID3_STATE; 
				if(!_link->sendCID(_nodes[index].node, 2))
				{
					_nodes[index].state = ALIAS_CID2_STATE; 
				}
				break;
			case ALIAS_CID3_STATE:
				//send CID3!
				//Serial.println("sending CID3");
				//_nodes[index].node->print();
				_nodes[index].state = ALIAS_CID4_STATE;
				if(!_link->sendCID(_nodes[index].node, 3))
				{
					_nodes[index].state = ALIAS_CID3_STATE; 
				}
				break;			
			case ALIAS_CID4_STATE:
				//send CID4!
				//Serial.println("sending CID4");
				//_nodes[index].node->print();
				_nodes[index].time_stamp = millis();
				_nodes[index].state = ALIAS_RID_STATE;
				if(!_link->sendCID(_nodes[index].node, 4))
				{
					_nodes[index].state = ALIAS_CID4_STATE; 
				}
				break;
			case ALIAS_RID_STATE:
				//maybe send RID
				if((millis() - _nodes[index].time_stamp) >= RID_TIME_WAIT)
				{
					//Serial.println("sending RID");
					//_nodes[index].node->print();
					if(_nodes[index].node) //if there is a real NodeID attached
						_nodes[index].state = ALIAS_AMD_STATE;
					else //no actual NodeID, so just go to the holding state.
						_nodes[index].state = ALIAS_HOLDING_STATE;
					if(!_link->sendRID(_nodes[index].node))
					{
						_nodes[index].state = ALIAS_RID_STATE;
					}
				}
				break;
			case ALIAS_RESENDRID_STATE:
				_nodes[index].state = ALIAS_READY_STATE;
				if(!_link->sendRID(_nodes[index].node))
				{
					_nodes[index].state = ALIAS_RESENDRID_STATE;
				}
				break;
			case ALIAS_SENDVERIFIEDNID_STATE:
				_nodes[index].state = ALIAS_READY_STATE;
				if(!_link->sendVerifiedNID(_nodes[index].node))
				{
					_nodes[index].state = ALIAS_SENDVERIFIEDNID_STATE;
				}
				break;
			case ALIAS_AMD_STATE:
				//Serial.println("sending AMD");
				//_nodes[index].node->print();

				_nodes[index].state = ALIAS_READY_STATE;
				if(!_link->sendAMD(_nodes[index].node))
				{
					_nodes[index].state = ALIAS_AMD_STATE;
				}
				else
				{
					_nodes[index].node->initialized = true;
				}
				break;
		}

  index = (index+1)%CAN_ALIAS_BUFFER_SIZE;
}

void OLCB_CAN_Alias_Helper::preAllocateAliases(void)
{
	//the idea here is to take every alias in the "Empty" state (i.e., that is unallocated), and to go ahead and allocate the sucker, moving it directly into the Initial state, rather than the AMD state.
	for(uint8_t i = 0; i < CAN_ALIAS_BUFFER_SIZE; ++i)
	{
		if(_nodes[i].state == ALIAS_EMPTY_STATE)
		{
			//we don't have a NID to calculate the lfsr initial values, so we'll use the current time instead.
			uint32_t lfsr1 = millis();
			uint32_t lfsr2 = _helper_value;
			_nodes[i].alias = (lfsr1 ^ lfsr2 ^ (lfsr1>>12) ^ (lfsr2>>12) )&0xFFF;
			_nodes[i].state = ALIAS_CID1_STATE;
		}
	}
}

void OLCB_CAN_Alias_Helper::allocateAlias(OLCB_NodeID* nodeID)
{
	nodeID->initialized = false;
	private_nodeID_t *slot = 0;
	//find a location for this nodeID in our list
	for(uint8_t i = 0; i < CAN_ALIAS_BUFFER_SIZE; ++i)
	{
		if(_nodes[i].state == ALIAS_HOLDING_STATE)
		{
			//Serial.println("allocate: found a slot w/alias!");
			slot = &(_nodes[i]);
			break;
		}
	}
	if(!slot) //no slots with aliases ready to go. try the empty slots
	{
		for(uint8_t i = 0; i < CAN_ALIAS_BUFFER_SIZE; ++i)
		{
			if(_nodes[i].state == ALIAS_EMPTY_STATE)
			{
				//Serial.println("allocate: found a slot w/o alias!");
				slot = &(_nodes[i]);
				break;
			}
		}
	}
	if(!slot)
	//SERIUS ERROR CONDITION! NO SPACE TO CACHE NODEID!!
	{
		//Serial.println("allocate: NO MORE SLOTS FOR NODEIDS!");
		//Serial.println(CAN_ALIAS_BUFFER_SIZE, DEC);
		while(1);
	}

	
		
	slot->node = nodeID;
	//does the slot already have an alias we can reuse?
	if(slot->alias)
	{
		//Serial.print("allocate: no need to allocate alias: ");
	    //Serial.println(slot->alias, DEC);
		slot->node->alias = slot->alias; //copy it into the nodeID
		slot->state = ALIAS_AMD_STATE; //ready to go! Just send an AMD
	}
	else //we'll need to generate and allocate an alias
	{
		//Serial.println("allocate: moving to CID1!");
		uint32_t lfsr1 = (((uint32_t)nodeID->val[0]) << 16) | (((uint32_t)nodeID->val[1]) << 8) | ((uint32_t)nodeID->val[2]);
		uint32_t lfsr2 = (((uint32_t)nodeID->val[3]) << 16) | (((uint32_t)nodeID->val[4]) << 8) | ((uint32_t)nodeID->val[5]);
		slot->alias = (lfsr1 ^ lfsr2 ^ (lfsr1>>12) ^ (lfsr2>>12) )&0xFFF;
		slot->node->alias = slot->alias;
		slot->state = ALIAS_CID1_STATE;
	}
	slot->node->print();
}

void OLCB_CAN_Alias_Helper::reAllocateAlias(private_nodeID_t* nodeID)
{
	uint32_t temp1 = ((nodeID->lfsr1<<9) | ((nodeID->lfsr2>>15)&0x1FF)) & 0xFFFFFF;
	uint32_t temp2 = (nodeID->lfsr2<<9) & 0xFFFFFF;
   
	// add
	nodeID->lfsr2 = nodeID->lfsr2 + temp2 + 0x7A4BA9l;
	nodeID->lfsr1 = nodeID->lfsr1 + temp1 + 0x1B0CA3l;
   
	// carry
	nodeID->lfsr1 = (nodeID->lfsr1 & 0xFFFFFF) | ((nodeID->lfsr2&0xFF000000) >> 24);
	nodeID->lfsr2 = nodeID->lfsr2 & 0xFFFFFF;

	nodeID->alias = (nodeID->lfsr1 ^ nodeID->lfsr2 ^ (nodeID->lfsr1>>12) ^ (nodeID->lfsr2>>12) )&0xFFF;
	
	if(nodeID->node)
	{
		nodeID->node->alias = nodeID->alias;
		nodeID->node->initialized = false;
	}
	
	nodeID->state = ALIAS_CID1_STATE;
}

bool OLCB_CAN_Alias_Helper::releaseAlias(OLCB_NodeID* nodeID)
{
	//find the node in our list
	for(uint8_t i = 0; i < CAN_ALIAS_BUFFER_SIZE; ++i)
	{
		if(*nodeID == *(_nodes[i].node)) //if have the same ID, NOT if both point to the same object
		{
			_nodes[i].state = ALIAS_RELEASING_STATE;
			_nodes[i].node->initialized = false;
			return true; //short circuit, should only appear once. This is perhaps not the best assumption ever. TODO
		}
	}
	return false; //should never reach here, unless we don't have the ID in our list
}

void OLCB_CAN_Alias_Helper::idleAlias(OLCB_NodeID* nodeID)
{
//	TODO
	//find the corresponding alias
	private_nodeID_t *slot = 0;
	for(uint8_t i = 0; i < CAN_ALIAS_BUFFER_SIZE; ++i)
	{
		if(nodeID == _nodes[i].node)
		{
			slot = &_nodes[i];
			break;
		}
	}
	
	if(!slot) //couldn't find it, nothing to release
	{
		return;
	}
	
	//remove it's NID
	slot->node = 0;
	//move it into the allocated, but waiting state
	slot->state = ALIAS_HOLDING_STATE;
}

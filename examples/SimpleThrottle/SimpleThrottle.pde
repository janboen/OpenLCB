// Permits control of a single locomotive with address 03.


#include <DCCPacket.h>
#include <DCCPacketQueue.h>
#include <DCCPacketScheduler.h>

#include <can.h>

#include <OpenLCB.h>

float fmap(float x, float in_min, float in_max, float out_min, float out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}


class DCC_Throttle : public OLCB_Virtual_Node, public OLCB_Datagram_Handler, public OLCB_Train_Controller
{
  public:
  
    void create(OLCB_Link *link, OLCB_NodeID *nid)
    {
      OLCB_Datagram_Handler::create(link,nid);
      OLCB_Virtual_Node::create(link,nid);
      Train_Controller_create((OLCB_Datagram_Handler *)this);
    }

  bool handleMessage(OLCB_Buffer *buffer)
    {
      return OLCB_Datagram_Handler::handleMessage(buffer);
    }
    
  void update(void)
  {
    Train_Controller_setSpeed_m_s(fmap(analogRead(A0), 0, 1023, 0, 55)); //divide by two to take a 0-1023 range number and make it 0-5 m/s range.
    if(isPermitted())
    {
      OLCB_Datagram_Handler::update();
      Train_Controller_update();
    }
  }

  void datagramResult(bool accepted, uint16_t errorcode)
  {
     Serial.print("The datagram was ");
     if(!accepted)
       Serial.print("not ");
     Serial.println("accepted.");
     if(!accepted)
     {
       Serial.print("   The reason: ");
       Serial.println(errorcode,HEX);
     }
  }
  
  void initialize(void)
  {
    Train_Controller_initialize();
  }
  
  bool processDatagram(void)
  {
    if(isPermitted() && (_rxDatagramBuffer->destination == *OLCB_Virtual_Node::NID))
    {
      return Train_Controller_processDatagram(_rxDatagramBuffer);
    }
  }
  
  
};

OLCB_NodeID train_nid(6,1,0,0,0,3);
OLCB_NodeID nid(6,1,0,0,0,4);
DCC_Throttle myThrottle;
OLCB_CAN_Link link;

void setup()
{
  Serial.begin(115200);
  
  Serial.println("SimpleThrottle begin!");
  
  link.initialize();
  myThrottle.initialize();
  myThrottle.create(&link, &nid);
  myThrottle.Train_Controller_attach(&train_nid);
  link.addVNode(&myThrottle);
  
  Serial.println("Initialization complete!");
}

void loop()
{
  //read analogl line for speed!
  link.update();
}

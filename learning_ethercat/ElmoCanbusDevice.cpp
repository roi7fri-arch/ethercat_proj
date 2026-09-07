#include <Devices/ElmoDevice/code/ElmoCanbusDevice.h>
#include <Config/CommunicationConfig/CommunicationConfig.h>
#include <MathMacros.h>
#include <cerrno>
#ifndef WIN32
    #include <unistd.h>
#endif

#define SEND_TELEMETRY_EVERY_MILLIES 50
//Not used #define RECOUNT_MAX_STEP_TIME 10000/SEND_TELEMETRY_EVERY_MILLIES
#define ENCODER_RESOLUTION 2097152
const int ELMO_TX_MSGLEN = sizeof(Mcu2Elmo);
using namespace InterfaceDefs;

ElmoCanbusDevice::ElmoCanbusDevice(std::string name, std::shared_ptr<rsiTopicsDispatcher> topicDispatcher, int nLoggerId,  std::shared_ptr <InternalStatistics> elmoStatistics, bool bRemoteCanbus) :
    rsiDevice(name, nLoggerId),
    m_ElmoFramer(nLoggerId),
    m_pTopicDispatcher(topicDispatcher.get()),
    m_pIncomingTopicEntry(nullptr),
    m_pOutgoingTopicEntry(nullptr),
    m_ElmoDeviceTopicServer(name + " TopicServer"),
    m_ElmoDeviceCapability(m_ElmoDeviceTopicServer),
    m_ElmoStatistics(elmoStatistics),
    m_bRemoteCanbus(bRemoteCanbus)
{
    m_sDisableTPDO1.byte0 = 0x22; //pages 53,47-48,192 at elmo's ds301 menual
    m_sDisableTPDO1.byte1 = 0x00;
    m_sDisableTPDO1.byte2 = 0x18;
    m_sDisableTPDO1.byte3 = 0x01;
    m_sDisableTPDO1.byte4 = 0x81;
    m_sDisableTPDO1.byte5 = 0x01;
    m_sDisableTPDO1.byte6 = 0x00;
    m_sDisableTPDO1.byte7 = 0x80;

    m_sMoCommand.byte0 = 0x22;
    m_sMoCommand.elmoObjectAddr = 0x3146;
    m_sMoCommand.subIndex = 0x1;
    m_sMoCommand.nData = 0x0;

    m_sTcCommand.byte0 = 0x22;
    m_sTcCommand.elmoObjectAddr = 0x31F0;
    m_sTcCommand.subIndex = 0x1;
    m_sTcCommand.flData = 0x0;

    m_sStCommand.byte0 = 0x22;
    m_sStCommand.elmoObjectAddr = 0x31E7;
    m_sStCommand.subIndex = 0x1;
    m_sStCommand.nData = 0x1;

    m_sUmCommand.byte0 = 0x22;
    m_sUmCommand.elmoObjectAddr = 0x3214;
    m_sUmCommand.subIndex = 0x1;
    m_sUmCommand.nData = 0x0;

	m_sTrTorqueSlope.byte0 = 0x22;
	m_sTrTorqueSlope.elmoObjectAddr = 0x6087;
	m_sTrTorqueSlope.subIndex = 0x1;
    m_sTrTorqueSlope.nData = ((200000.0/ m_nTrMotorRatedCurrent)*1000*1000);//first 1000 is by elmos formula sec 1000 is conversion from miliAmp to amp

    m_sElTorqueSlope.byte0 = 0x22;
    m_sElTorqueSlope.elmoObjectAddr = 0x6087;
    m_sElTorqueSlope.subIndex = 0x1;
    m_sElTorqueSlope.nData = ((200000.0/ m_nElMotorRatedCurrent)*1000*1000);//first 1000 is by elmos formula sec 1000 is conversion from miliAmp to amp

    m_TorqueProfileMode.byte0 = 0x2F;
    m_TorqueProfileMode.byte1 = 0x60;
    m_TorqueProfileMode.byte2 = 0x60;
    m_TorqueProfileMode.byte3 = 0x00;
    m_TorqueProfileMode.byte4 = 0x04;
    m_TorqueProfileMode.byte5 = 0x00;
    m_TorqueProfileMode.byte6 = 0x00;
    m_TorqueProfileMode.byte7 = 0x00;

    m_sStatusWordRequest.x = 0x0;
    m_sStatusWordRequest.css = 0x2;
    m_sStatusWordRequest.m = 0x6041;
    m_sStatusWordRequest.reserved = 0x0;

    m_sVuRequest.x = 0x0;
    m_sVuRequest.css = 0x2;
    m_sVuRequest.m = 0x606C;
    m_sVuRequest.reserved = 0x0;

    m_sVxRequest.x = 0x0;
    m_sVxRequest.css = 0x2;
    m_sVxRequest.m = 0x6069;
    m_sVxRequest.reserved = 0x0;

    m_sSrRequest.x = 0x0;
    m_sSrRequest.css = 0x2;
    m_sSrRequest.m = 0x1002;
    m_sSrRequest.reserved = 0x0;

    memset(&m_TrRPDO1,0,sizeof(RPDO1));
    memset(&m_ElRPDO1,0,sizeof(RPDO1));
    memset(&m_TrTPDO1,0,sizeof(TPDO1));
    memset(&m_ElTPDO1,0,sizeof(TPDO1));

	memset(&m_StepMessage, 0, sizeof(CanbusAdapterFramer::CanbusAdapterStepMessage));
	m_StepMessage.header.opcode = CanbusAdapterFramer::Comx2Arm_ECanbusAdapterMsgOpcodes_Step;

	m_StepMessage.canMsg[CanbusAdapterFramer::EStepMessages_ElRPDO1].COB_ID = MDCU_2_ELMO_EL_RPDO1;
    m_StepMessage.canMsg[CanbusAdapterFramer::EStepMessages_ElRPDO1].DATA_LENGTH = sizeof(m_ElRPDO1);
    m_StepMessage.canMsg[CanbusAdapterFramer::EStepMessages_ElRPDO1].RTR = 0;

	m_StepMessage.canMsg[CanbusAdapterFramer::EStepMessages_TrRPDO1].COB_ID = MDCU_2_ELMO_TR_RPDO1;
    m_StepMessage.canMsg[CanbusAdapterFramer::EStepMessages_TrRPDO1].DATA_LENGTH = sizeof(m_TrRPDO1);
    m_StepMessage.canMsg[CanbusAdapterFramer::EStepMessages_TrRPDO1].RTR = 0;

	m_StepMessage.canMsg[CanbusAdapterFramer::EStepMessages_Sync].COB_ID = MDCU_2_ELMO_SYNC_COB_ID;
    m_StepMessage.canMsg[CanbusAdapterFramer::EStepMessages_Sync].DATA_LENGTH = sizeof(m_sync);
    m_StepMessage.canMsg[CanbusAdapterFramer::EStepMessages_Sync].RTR = 0;
}

ElmoCanbusDevice::~ElmoCanbusDevice()
{
    EnableDisableMotors(false);
}

rsiCommFramer* ElmoCanbusDevice::getFramer()
{
    if(m_bRemoteCanbus)
        return &m_CanAdapterFramer;
    else
        return &m_ElmoFramer;
}

void ElmoCanbusDevice::InitErrorMap()
{
    m_errorMap =
    {
       {ELMO_ERROR_CODE_Short_circuit , " Short_circuit"},
       {ELMO_ERROR_CODE_Under_voltage , " Under_voltage"},
       {ELMO_ERROR_CODE_AC_fail_loss_of_phase , " AC_fail_loss_of_phase"},
       {ELMO_ERROR_CODE_Over_voltage , " Over_voltage"},
       {ELMO_ERROR_CODE_Temperature_drive_overheating  , " Temperature_drive_overheating"},
       {ELMO_ERROR_CODE_Gantry_position_error , " Gantry_position_error "},
       {ELMO_ERROR_CODE_Motor_disabled_by_INHIBIT_or_ABORT , " Motor_disabled_by_INHIBIT_or_ABORT"},
       {ELMO_ERROR_CODE_Motor_disabled_by_switch_additional_abort_motion  , " Motor_disabled_by_switch_additional_abort_motion"},
       {ELMO_ERROR_CODE_RPDO_failed , " RPDO_failed"},
       {ELMO_ERROR_CODE_RPDO_failed , " RPDO_failed"},
       {ELMO_ERROR_CODE_Feedback_error , " Feedback_error"},
       {ELMO_ERROR_CODE_Two_digital_Hall_sensors_were_changed_at_the_same_time  , " Two_digital_Hall_sensors_were_changed_at_the_same_time"},
       {ELMO_ERROR_CODE_Commutation_process_fail_during_motor_on , " Commutation_process_fail_during_motor_on"},
       {ELMO_ERROR_CODE_CAN_message_lost_corrupted_or_overrun , " CAN_message_lost_corrupted_or_overrun"},
       {ELMO_ERROR_CODE_Heartbeat_event , " Heartbeat_event"},
       {ELMO_ERROR_CODE_Recovered_from_bus_off , " Recovered_from_bus_off"} ,
       {ELMO_ERROR_CODE_Attempt_to_access_a_non_configured_RPDO , " Attempt_to_access_a_non_configured_RPDO"},
       {ELMO_ERROR_CODE_Peak_current_has_been_exceeded_Possible_reasons_are_drive_malfunction_or_bad_tuning_of_the_current_controller , " Peak_current_has_been_exceeded_Possible_reasons_are_drive_malfunction_or_bad_tuning_of_the_current_controller"},
       {ELMO_ERROR_CODE_Speed_tracking_error , " Speed_tracking_error"},
       {ELMO_ERROR_CODE_Speed_limit_exceeded , " Speed_limit_exceeded"},
       {ELMO_ERROR_CODE_Position_tracking_error , " Position_tracking_error"},
       {ELMO_ERROR_CODE_Position_limit_exceeded , " Position_limit_exceeded"},
       {ELMO_ERROR_CODE_Request_by_user_program_EMCY_function , " Request_by_user_program_EMCY_function"},
       {ELMO_ERROR_CODE_IP_mode_underflow_or_Interpolation_queue_full_or_Reference_received_in_a_wrong_index_or_Bad_PVT_send_order  , " IP_mode_underflow_or_Interpolation_queue_full_or_Reference_received_in_a_wrong_index_or_Bad_PVT_send_order"},
       {ELMO_ERROR_CODE_Failed_to_start_motor , " Failed_to_start_motor"},
       {ELMO_ERROR_CODE_Safety_Torque_Off_in_use , " Safety_Torque_Off_in_use"},
       {ELMO_ERROR_CODE_Gantry_Slave_Disabled , " Gantry_Slave_Disabled"}
    };
}

bool ElmoCanbusDevice::Initialize()
{
    if(nullptr != m_pCommChannel)
    {
        // Register for incoming comm msgs
        m_pCommChannel->incoming.Subscribe([this](const uint8_t *data, uint32_t size) { HandleIncomingMessageFromCommunicationChannel(data, size); }, m_pTopicDispatcher, this);
        m_statisticsTopicServer->commOK.Subscribe([this](const InterfaceDefs::CommOK& data) { HandleStatisticCommData(data); }, m_pTopicDispatcher,this);
    }
    m_pDriveEnableLogicTopicServer->DRIVE_ENABLE_SWITCH_CHANGED.Subscribe([this](const eButtonEnum& button) { HandleDriveEnableSwitchChanged(button); }, m_pTopicDispatcher, this, true);
    // Register for device's command topics
    m_ElmoDeviceTopicServer.ELMO_EL_MOTOR_ENABLE_DISABLE_COMMAND.Subscribe([this](bool pCmd) { SendElMotorEnableDisableCommand(pCmd); }, m_pTopicDispatcher, this);
    m_ElmoDeviceTopicServer.ELMO_TR_MOTOR_ENABLE_DISABLE_COMMAND.Subscribe([this](bool pCmd) { SendTrMotorEnableDisableCommand(pCmd); }, m_pTopicDispatcher, this);
    m_ElmoDeviceTopicServer.ELMO_EL_MOTOR_CURRENT_COMMAND.Subscribe([this](float pCmd) { SendElTcCommand(pCmd); }, m_pTopicDispatcher, this);
    m_ElmoDeviceTopicServer.ELMO_TR_MOTOR_CURRENT_COMMAND.Subscribe([this](float pCmd) { SendTrTcCommand(pCmd); }, m_pTopicDispatcher, this);
    m_ElmoDeviceTopicServer.ELMO_EL_STOP_COMMAND.Subscribe([this]() { SendElStopProfilerCommand(); }, m_pTopicDispatcher, this);
    m_ElmoDeviceTopicServer.ELMO_TR_STOP_COMMAND.Subscribe([this]() { SendTrStopProfilerCommand(); }, m_pTopicDispatcher, this);
    m_ElmoDeviceTopicServer.ELMO_EL_UNIT_MODE_COMMAND.Subscribe([this](UM_ENUM pCmd) { SendElUmCommand(pCmd); }, m_pTopicDispatcher, this);
    m_ElmoDeviceTopicServer.ELMO_TR_UNIT_MODE_COMMAND.Subscribe([this](UM_ENUM pCmd) { SendTrUmCommand(pCmd); }, m_pTopicDispatcher, this);
    m_ElmoDeviceTopicServer.ELMO_EL_STATUS_WORD_REQUEST.Subscribe([this]() { SendElSWRequest(); }, m_pTopicDispatcher, this);
    m_ElmoDeviceTopicServer.ELMO_TR_STATUS_WORD_REQUEST.Subscribe([this]() { SendTrSWRequest(); }, m_pTopicDispatcher, this);
    m_ElmoDeviceTopicServer.ELMO_EL_VELOCITY_FEEDBACK_REQUEST.Subscribe([this]() { SendElVuRequest(); }, m_pTopicDispatcher, this);
    m_ElmoDeviceTopicServer.ELMO_TR_VELOCITY_FEEDBACK_REQUEST.Subscribe([this]() { SendTrVuRequest(); }, m_pTopicDispatcher, this);

    m_TelemetryTimerId = m_pTopicDispatcher->RegisterEventTimer(SEND_TELEMETRY_EVERY_MILLIES,0,[this](){SendTelemetryData();});
#ifndef SYS_SIM
	m_ElmoConfigMotorTimerId = m_pTopicDispatcher->RegisterEventTimer(3000, 0, [this]() {ElmoConfigtMotorTimerFunction(); });
#else
	m_ElmoConfigMotorTimerId = m_pTopicDispatcher->RegisterEventTimer(30, 0, [this]() {ElmoConfigtMotorTimerFunction(); });
#endif
    m_ElStartMotorTimerId = m_pTopicDispatcher->RegisterEventTimer(30,0,[this](){ElStartMotorTimerFunction();});
    m_TrStartMotorTimerId = m_pTopicDispatcher->RegisterEventTimer(30,0,[this](){TrStartMotorTimerFunction();});
    m_SdoConfigTimerId = m_pTopicDispatcher->RegisterEventTimer(10,0,[this](){DoSdoConfigTimerFunction();});
    m_debugTimerId = m_pTopicDispatcher->RegisterEventTimer(20,0,[this](){});

    m_ElmoElRpdo1MappingTimerId = m_pTopicDispatcher->RegisterEventTimer(10,0,[this](){ElmoElRpdo1Mapping();});
    m_ElmoElTpdo1MappingTimerId = m_pTopicDispatcher->RegisterEventTimer(10,0,[this](){ElmoElTpdo1Mapping();});
    m_ElmoTrRpdo1MappingTimerId = m_pTopicDispatcher->RegisterEventTimer(10,0,[this](){ElmoTrRpdo1Mapping();});
    m_ElmoTrTpdo1MappingTimerId = m_pTopicDispatcher->RegisterEventTimer(10,0,[this](){ElmoTrTpdo1Mapping();});
    InitErrorMap();
    return true;
}


void ElmoCanbusDevice::InitConnections(StatisticsTopicServer* pStatisticsTopicServer,
                                       WIUActivityTopics* pWIUActivityTopics,
                                       DriveEnableLogicTopicServer* pDriveEnableLogicTopicServer)
{
    m_statisticsTopicServer = pStatisticsTopicServer;
    m_pWIUActivityTopics = pWIUActivityTopics;
    m_pDriveEnableLogicTopicServer = pDriveEnableLogicTopicServer;
}

void ElmoCanbusDevice::InitAndStartMotors()
{
   m_pTopicDispatcher->StopTimer(m_ElmoConfigMotorTimerId);
   m_pTopicDispatcher->StopTimer(m_ElStartMotorTimerId);
   m_pTopicDispatcher->StopTimer(m_TrStartMotorTimerId);
   m_pTopicDispatcher->StopTimer(m_SdoConfigTimerId);
   m_pTopicDispatcher->StopTimer(m_ElmoElRpdo1MappingTimerId);
   m_pTopicDispatcher->StopTimer(m_ElmoElTpdo1MappingTimerId);
   m_pTopicDispatcher->StopTimer(m_ElmoTrRpdo1MappingTimerId);
   m_pTopicDispatcher->StopTimer(m_ElmoTrTpdo1MappingTimerId);

    m_bElNmtResponse = false;
    m_bTrNmtResponse = false;
    m_bIsInInit = true;
    m_bIsElMotorOk = false;
    m_bIsTrMotorOk = false;
    m_bPdoMappingCompleted = false;
    m_bIsElMotorOperational = false;
    m_bStepsNotAllowd = true;

    m_pTopicDispatcher->StartTimer(m_ElmoConfigMotorTimerId);
}

void ElmoCanbusDevice::Run()
{
    // Start the timer.
    if(nullptr != m_pCommChannel)
    {
        m_pTopicDispatcher->StartTimer(m_TelemetryTimerId);
    }
    InitAndStartMotors();
}

void ElmoCanbusDevice::HandleDriveEnableSwitchChanged(const eButtonEnum& button)
{
    if(BUTTON_ON == button)
    {
        if(!m_bIsElMotorOk)
        {
            SetElMotorOn();
        }
        if(!m_bIsTrMotorOk)
        {
            SetTrMotorOn();
        }
    }
}

void ElmoCanbusDevice::DoElmoSdoConfig()
{
    m_pTopicDispatcher->StartTimer(m_SdoConfigTimerId);
}

void ElmoCanbusDevice::ElmoConfigtMotorTimerFunction()
{
     if(true == m_bElNmtResponse && true == m_bTrNmtResponse)
    {
        m_pTopicDispatcher->StopTimer(m_ElmoConfigMotorTimerId);
#ifndef WIN32
    usleep(1000);
#endif
        SendNmtStartCommand();

        SendElSWRequest();
        SendTrSWRequest();
        DoElmoPdoMapping();
        DoElmoSdoConfig();
        SetElMotorOn();
        SetTrMotorOn();

    }
    else
    {
         m_nCounter++;
         if(m_nCounter > 1)
         {
             if(!m_bElNmtResponse && !m_bTrNmtResponse)
             {
                 SendNmtResetCommCommand();
             }
             else
             {
                 if(!m_bElNmtResponse)
                 {
                  SendNmtResetCommCommand(EL_NODE_ID);
                 }
                 if(!m_bTrNmtResponse)
                 {
                  SendNmtResetCommCommand(TR_NODE_ID);
                 }
             }
             m_nCounter = 0;
         }
    }
}

void ElmoCanbusDevice::ElStartMotorTimerFunction()
{
   if(!m_bPdoMappingCompleted || m_bIsInInit)
   {
       return;
   }

   ELMO_SW_ENUM swResult = ParseStatusWord(m_ElTPDO1.sw);
   switch(swResult)
   {
    case ELMO_SW_FAULT:
       {
           m_bIsElMotorOperational = false;
           SendResetErrorsCommandEl();
           SendElSWRequest();
           rsiLOG(DEBUG_LEVEL) <<"sent El ELMO_SW_FAULT";
           break;
       }

    case ELMO_SW_SEND_STAGE_1: //240
       {
       m_bIsElMotorOperational = false;
       sElmoTerminalCommand cmd;
       cmd.byte0 = 0x22;
       cmd.elmoObjectAddr = 0x6040;
       cmd.subIndex = 0x1;

       memset(&m_ElRPDO1.cw,0,sizeof(ControlWord));

       m_ElRPDO1.cw.enableVoltage = 1;
       m_ElRPDO1.cw.quikStop = 1;
       memcpy(&cmd.nData,&m_ElRPDO1.cw,sizeof(ControlWord));
       SendCommMsg(&cmd,MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
       m_ElmoDeviceTopicServer.ELMO_EL_CRITICAL_ERROR.Publish(false);
       SendElSWRequest();
       rsiLOG(DEBUG_LEVEL) <<"sent El ELMO_SW_SEND_STAGE_1";
       break;
       }
   case ELMO_SW_SEND_STAGE_2://221 //223
      {
      m_bIsElMotorOperational = false;
      sElmoTerminalCommand cmd;
      cmd.byte0 = 0x22;
      cmd.elmoObjectAddr = 0x6040;
      cmd.subIndex = 0x1;

      memset(&m_ElRPDO1.cw,0,sizeof(ControlWord));

      m_ElRPDO1.cw.switchOn = 1;
      m_ElRPDO1.cw.enableVoltage = 1;
      m_ElRPDO1.cw.quikStop = 1;
      memcpy(&cmd.nData,&m_ElRPDO1.cw,sizeof(ControlWord));
      SendCommMsg(&cmd,MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
      SendElSWRequest();
      rsiLOG(DEBUG_LEVEL) <<"sent El ELMO_SW_SEND_STAGE_2";
      break;
      }
   case ELMO_SW_MOTOR_ON:
      {
      m_ElRPDO1.cw.switchOn = 1;
      m_ElRPDO1.cw.enableVoltage = 1;
      m_ElRPDO1.cw.quikStop = 1;
      m_bIsElMotorOperational = false;
      m_pTopicDispatcher->StopTimer(m_ElStartMotorTimerId);
      m_bIsElMotorOk = true;
      m_bStepsNotAllowd = false;
      rsiLOG(DEBUG_LEVEL) <<"sent El ELMO_SW_MOTOR_ON";
      break;
      }
    case ELMO_SW_MOTOR_OPERATIONAL:
       {
       m_bIsElMotorOperational = true;

       //Enable operation
       m_ElRPDO1.cw.switchOn = 1;
       m_ElRPDO1.cw.enableVoltage = 1;
       m_ElRPDO1.cw.quikStop = 1;
       m_ElRPDO1.cw.enableOperation = 1;
       m_pTopicDispatcher->StopTimer(m_ElStartMotorTimerId);
       m_bIsElMotorOk = true;
       m_bStepsNotAllowd = false;
       rsiLOG(DEBUG_LEVEL) <<"sent El ELMO_SW_MOTOR_OPERATIONAL";
       break;
       }

   }//switch
   PrintElSW();
}

void ElmoCanbusDevice::TrStartMotorTimerFunction()
{
    if(!m_bPdoMappingCompleted || m_bIsInInit)
    {
        return;
    }
    ELMO_SW_ENUM swResult = ParseStatusWord(m_TrTPDO1.sw);
    switch(swResult)
    {
     case ELMO_SW_FAULT:
        {
            m_bIsTrMotorOperational = false;
            SendResetErrorsCommandTr();
            SendTrSWRequest();
            rsiLOG(DEBUG_LEVEL) <<"sent Tr ELMO_SW_FAULT";
            break;
        }

     case ELMO_SW_SEND_STAGE_1: //240
        {
        m_bIsTrMotorOperational = false;
        sElmoTerminalCommand cmd;
        cmd.byte0 = 0x22;
        cmd.elmoObjectAddr = 0x6040;
        cmd.subIndex = 0x1;

        memset(&m_TrRPDO1.cw,0,sizeof(ControlWord));

        m_TrRPDO1.cw.enableVoltage = 1;
        m_TrRPDO1.cw.quikStop = 1;
        memcpy(&cmd.nData,&m_TrRPDO1.cw,sizeof(ControlWord));
        SendCommMsg(&cmd,MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
        m_ElmoDeviceTopicServer.ELMO_TR_CRITICAL_ERROR.Publish(false);
        rsiLOG(DEBUG_LEVEL) <<"sent Tr ELMO_SW_SEND_STAGE_1";
        SendTrSWRequest();
        break;
        }

    case ELMO_SW_SEND_STAGE_2://221 //223
       {
       m_bIsTrMotorOperational = false;
       sElmoTerminalCommand cmd;
       cmd.byte0 = 0x22;
       cmd.elmoObjectAddr = 0x6040;
       cmd.subIndex = 0x1;

       memset(&m_TrRPDO1.cw,0,sizeof(ControlWord));

       m_TrRPDO1.cw.switchOn = 1;
       m_TrRPDO1.cw.enableVoltage = 1;
       m_TrRPDO1.cw.quikStop = 1;
       memcpy(&cmd.nData,&m_TrRPDO1.cw,sizeof(ControlWord));
       SendCommMsg(&cmd,MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
        rsiLOG(DEBUG_LEVEL) <<"sent Tr ELMO_SW_SEND_STAGE_2";
       SendTrSWRequest();
       break;
       }
    case ELMO_SW_MOTOR_ON:
       {
       m_TrRPDO1.cw.switchOn = 1;
       m_TrRPDO1.cw.enableVoltage = 1;
       m_TrRPDO1.cw.quikStop = 1;
       m_bIsTrMotorOperational = false;
       m_pTopicDispatcher->StopTimer(m_TrStartMotorTimerId);
       m_bIsTrMotorOk = true;
       m_bStepsNotAllowd = false;
       rsiLOG(DEBUG_LEVEL) <<"Tr ELMO_SW_MOTOR_ON";
       break;
       }
     case ELMO_SW_MOTOR_OPERATIONAL:
        {
        m_bIsTrMotorOperational = true;
        //Enable operation
        m_TrRPDO1.cw.switchOn = 1;
        m_TrRPDO1.cw.enableVoltage = 1;
        m_TrRPDO1.cw.quikStop = 1;
        m_TrRPDO1.cw.enableOperation = 1;
        m_pTopicDispatcher->StopTimer(m_TrStartMotorTimerId);
        m_bIsTrMotorOk = true;
        m_bStepsNotAllowd = false;
        break;
        }

    }//switch
    PrintTrSW();
}
void ElmoCanbusDevice::DoSdoConfigTimerFunction()
{
    if(!m_bPdoMappingCompleted)
    {
        return;
    }
    else
    {
        m_pTopicDispatcher->StopTimer(m_SdoConfigTimerId);
            SendTorqueProfileMode(EL_NODE_ID);
			#ifndef WIN32
				usleep(10);
			#endif
            SendTorqueProfileMode(TR_NODE_ID);
			#ifndef WIN32
				usleep(10);
			#endif
            MaxContinuousCurrentRequest(EL_NODE_ID);
			#ifndef WIN32
				usleep(10);
			#endif
            MaxContinuousCurrentRequest(TR_NODE_ID);
			#ifndef WIN32
				usleep(10);
			#endif
            SendElTorqueSlopeCommand();
            #ifndef WIN32
                usleep(10);
            #endif
            SendTrTorqueSlopeCommand();
			#ifndef WIN32
                usleep(10);
			#endif
            m_bIsInInit = false;
    }
}


 ELMO_SW_ENUM ElmoCanbusDevice::ParseStatusWord(StatusWord sw){
     if(sw.fault)
     {
         return ELMO_SW_FAULT;
     }
     else if(sw.switchOnDisabled && !(sw.readyToSwitchOn))//240 -- > 221
     {
         return ELMO_SW_SEND_STAGE_1;
     }
     else if(sw.readyToSwitchOn && sw.quickStop && !(sw.switchedOn))//221 --> 223
     {
         return ELMO_SW_SEND_STAGE_2;
     }
     else if(sw.readyToSwitchOn && sw.quickStop && sw.switchedOn && !(sw.operationEnabled) /*&& sw.voltageEnabled*/)
     {
         return ELMO_SW_MOTOR_ON;
     }
     else if(sw.readyToSwitchOn && sw.quickStop && sw.switchedOn && sw.operationEnabled && sw.voltageEnabled)
     {
         return ELMO_SW_MOTOR_OPERATIONAL;
     }
	 return ELMO_SW_FAULT;
 }

void ElmoCanbusDevice::EnableDisableMotors(bool onOff)
{

    if (m_objectDisable || m_bDebug)
	{
		return;
	}

    if(m_bLastMotorEnableDisable != onOff)
    {
        SendElMotorEnableDisableCommand(onOff);
        SendTrMotorEnableDisableCommand(onOff);
        m_bLastMotorEnableDisable = onOff;
        m_sElmoDataReport.bEnableCommand = onOff;
    }
}


void ElmoCanbusDevice::VuRequest()
{
    if (m_objectDisable || m_bDebug)
	{
		return;
	}

    SendElVuRequest();
    SendTrVuRequest();
}

void ElmoCanbusDevice::VxRequest()
{
    if (m_objectDisable || m_bDebug )
	{
		return;
	}
    if(nullptr != m_pCommChannel)
    {
        return;
    }
    SendElVxRequest();
    SendTrVxRequest();
}

void ElmoCanbusDevice::SrRequest()
{
if (m_objectDisable || m_bDebug)
	{
		return;
	}
    if(nullptr != m_pCommChannel)
    {
        return;
    }
    SendElSrRequest();
    SendTrSrRequest();
}

void ElmoCanbusDevice::SetChannel(std::unique_ptr<CommChannel> channel)
{
    m_pCommChannel = std::move(channel);
}

void ElmoCanbusDevice::HandleStatisticCommData(const InterfaceDefs::CommOK&  data)
{
    if(data.isCommOK == true )
    {
        if(m_eELMO_COM != ELMO_COM_OK)
        {
            m_eELMO_COM = ELMO_COM_OK;
            rsiLOG(INFO_LEVEL) << "ELMO CONNECTED " ;
            return;
        }
    }
    else
    {
        if(m_eELMO_COM != ELMO_NO_COM)
        {
            m_eELMO_COM = ELMO_NO_COM;
            rsiLOG(INFO_LEVEL) << "ELMO DISCONNECTED " ;
            InitAndStartMotors();
            return;
        }
    }
}

void ElmoCanbusDevice::GetCommandByAdrr(char* command)
{

    if(m_pElmoResponse->m_index == 0x3146)
    {
       command[0] = 'M';
       command[1] = 'O';
       command[2] = '\0';
    }
    else if(m_pElmoResponse->m_index == 0x31F0)
    {
        command[0] = 'T';
        command[1] = 'C';
        command[2] = '\0';
    }
    else if(m_pElmoResponse->m_index == 0x31E7)
    {
        command[0] = 'S';
        command[1] = 'T';
        command[2] = '\0';
    }
    else if(m_pElmoResponse->m_index == 0x3214)
    {
        command[0] = 'U';
        command[1] = 'M';
        command[2] = '\0';
    }

}

void ElmoCanbusDevice::PrintElSW()
{
    rsiLOG(DEBUG_LEVEL)<<"GOT ELMO EL Status word: " <<"\n"
    <<" switchedOn: "<<m_ElTPDO1.sw.switchedOn <<"\n"
    <<" operationEnabled: "<<m_ElTPDO1.sw.operationEnabled <<"\n"
    <<" fault: "<<m_ElTPDO1.sw.fault <<"\n"
    <<" voltageEnabled: "<<m_ElTPDO1.sw.voltageEnabled <<"\n"
    <<" quickStop: "<<m_ElTPDO1.sw.quickStop <<"\n"
    <<" switchOnDisabled: "<<m_ElTPDO1.sw.switchOnDisabled <<"\n"
    <<" warning: " <<m_ElTPDO1.sw.warning <<"\n"
    <<" remote: "<<m_ElTPDO1.sw.remote<<"\n"
    <<" targetReached: " <<m_ElTPDO1.sw.targetReached <<"\n"
    <<" internalLimitActive: "<<m_ElTPDO1.sw.internalLimitActive <<"\n"
    <<" operationalModeSpecific: " <<m_ElTPDO1.sw.operationalModeSpecific  ;
}


void ElmoCanbusDevice::PrintTrSW()
{
    rsiLOG(DEBUG_LEVEL)<<"ELMO TR Status word: " <<"\n"
    <<" switchedOn: "<<m_TrTPDO1.sw.switchedOn <<"\n"
    <<" operationEnabled: "<<m_TrTPDO1.sw.operationEnabled <<"\n"
    <<" fault: "<<m_TrTPDO1.sw.fault <<"\n"
    <<" voltageEnabled: "<<m_TrTPDO1.sw.voltageEnabled <<"\n"
    <<" quickStop: "<<m_TrTPDO1.sw.quickStop <<"\n"
    <<" switchOnDisabled: "<<m_TrTPDO1.sw.switchOnDisabled <<"\n"
    <<" warning: " <<m_TrTPDO1.sw.warning <<"\n"
    <<" remote: "<<m_TrTPDO1.sw.remote<<"\n"
    <<" targetReached: " <<m_TrTPDO1.sw.targetReached <<"\n"
    <<" internalLimitActive: "<<m_TrTPDO1.sw.internalLimitActive <<"\n"
    <<" operationalModeSpecific: " <<m_TrTPDO1.sw.operationalModeSpecific  ;
}

void ElmoCanbusDevice::SendResetErrorsCommandEl()
{
    sElmoTerminalCommand cmd;
    cmd.byte0 = 0x22;
    cmd.elmoObjectAddr = 0x6040;
    cmd.subIndex = 0x1;

    memset(&m_ElRPDO1.cw,0,sizeof(ControlWord));
    m_ElRPDO1.cw.FaultReset = 1;
    memcpy(&cmd.nData,&m_ElRPDO1.cw,sizeof(ControlWord));
    SendCommMsg(&cmd,MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
    m_ElRPDO1.cw.FaultReset = 0;
    rsiLOG(DEBUG_LEVEL)<<"Elmo Reset El";
}

void ElmoCanbusDevice::SendResetErrorsCommandTr()
{
    sElmoTerminalCommand cmd;
    cmd.byte0 = 0x22;
    cmd.elmoObjectAddr = 0x6040;
    cmd.subIndex = 0x1;

    memset(&m_TrRPDO1.cw,0,sizeof(ControlWord));
    m_TrRPDO1.cw.FaultReset = 1;
    memcpy(&cmd.nData,&m_TrRPDO1.cw,sizeof(ControlWord));
    SendCommMsg(&cmd,MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
    m_TrRPDO1.cw.FaultReset = 0;
    rsiLOG(DEBUG_LEVEL)<<"ELMO Reset tr";
}

void ElmoCanbusDevice::ClearErrorsEl()
{
    m_bStepsNotAllowd = true;
    m_bIsElMotorOk = false;
    SetElMotorOn();
}
void ElmoCanbusDevice::ClearErrorsTr()
{
    m_bStepsNotAllowd = true;
    m_bIsTrMotorOk = false;
    SetTrMotorOn();
}

void ElmoCanbusDevice::ClearErrors()
{
    if(!m_bIsInInit)
    {
      ClearErrorsEl();
      ClearErrorsTr();
    }
}

bool ElmoCanbusDevice::IsResetAllowed(ELMO_ERROR_CODES err)
{
    if(err == ELMO_ERROR_CODE_Short_circuit ||
       err == ELMO_ERROR_CODE_Under_voltage ||
       err == ELMO_ERROR_CODE_AC_fail_loss_of_phase ||
       err == ELMO_ERROR_CODE_Over_voltage ||
       err == ELMO_ERROR_CODE_Temperature_drive_overheating ||
       err == ELMO_ERROR_CODE_Motor_stuck ||
       err == ELMO_ERROR_CODE_Feedback_error ||
       err == ELMO_ERROR_CODE_Two_digital_Hall_sensors_were_changed_at_the_same_time ||
       err == ELMO_ERROR_CODE_Commutation_process_fail_during_motor_on ||
       err == ELMO_ERROR_CODE_Peak_current_has_been_exceeded_Possible_reasons_are_drive_malfunction_or_bad_tuning_of_the_current_controller)
    {
        return false;
    }
    return true;
}

void ElmoCanbusDevice::HandleIncomingMessageFromCommunicationChannel(const uint8_t *data, uint32_t size)
{
    //unused int64_t roundTripTime = rsiTime::currentSystemTimeMicros() - m_sendTimeTag;
//    rsiLOG(INFO_LEVEL) << "time measure = " << roundTripTime / 2 << " micro";
    if(m_bRemoteCanbus)
        data = data + 3; // +3 for sync+opcode bytes

    CanOpenMsg* pCommMsg = ( CanOpenMsg*)data;

    auto id = pCommMsg->COB_ID;

	static int nElErrCount = 0;
	static int nTrErrCount = 0;
    switch(id)
    {
        case ELMO_2_MDCU_EL_EMERGENCY:
        {
            try
            {
                sElmoEmergency tempEmg;
                memcpy(&tempEmg,pCommMsg->pchBuffer,(pCommMsg->DATA_LENGTH));
                rsiLOG_Oper(ERR_LEVEL)<<"GOT ELMO EL Emargency - "<<m_errorMap.at((ELMO_ERROR_CODES)tempEmg.ErrorCode)<<" Elmo err code: "<<tempEmg.ElmoErrorCode <<" Data field: "<<tempEmg.ErrorCodeDataField;
				m_sErrorReport.m_nElErrorArr[nElErrCount++ % 10] = (ELMO_ERROR_CODES)tempEmg.ErrorCode;
				m_ElmoDeviceTopicServer.ELMO_ERROR_REPORT.Publish(m_sErrorReport);
				m_sElmoDataReport.lastElElmoEmergency = tempEmg;
                if(m_sElmoDataReport.lastElElmoEmergency.ErrorCode == ELMO_ERROR_CODE_Attempt_to_access_a_non_configured_RPDO)
                {
                    m_bStepsNotAllowd = true;
                    ElmoTpdo1Mapping(EL_NODE_ID);
                    ElmoRpdo1Mapping(EL_NODE_ID);
                    m_bStepsNotAllowd = false;
                    rsiLOG(ERR_LEVEL)<<"ELMO EL RESET PDO MAPPING ";
                }
                if(!m_bPdoMappingCompleted || m_bIsInInit)
                {
                    rsiLOG(ERR_LEVEL)<<"ELMO EL - ERROR IN INITIALIZATION - RESTARTING PDO MAPPING";
                    InitAndStartMotors();
                }
                /////////////////////////////////////////
                ///////////////////////////////////////// delete this if block on ipcan!!
                if(ELMO_ERROR_CODE_CAN_message_lost_corrupted_or_overrun == (ELMO_ERROR_CODES)tempEmg.ErrorCode)
                {
                  rsiLOG(ERR_LEVEL)<<"GOT ELMO EL Emargency - "<<m_errorMap.at((ELMO_ERROR_CODES)tempEmg.ErrorCode)<<" Elmo err code: "<<tempEmg.ElmoErrorCode <<" Data field: "<<tempEmg.ErrorCodeDataField;
                }
                /////////////////////////////////////////
                ////////////////////////////////////////
                else if (IsResetAllowed((ELMO_ERROR_CODES)tempEmg.ErrorCode))
                {
                    ClearErrorsEl();
                    rsiLOG(ERR_LEVEL)<<"ELMO EL CLEAR ERRORS";
                }
                else //critical error
                {
                    m_bIsElMotorOk = false;
                    m_bIsInCriticalErrEl = true;
                    m_ElmoDeviceTopicServer.ELMO_EL_CRITICAL_ERROR.Publish(true);
                    rsiLOG_Oper(ERR_LEVEL)<<"GOT ELMO EL CRITICAL ERROR - "<<m_errorMap.at((ELMO_ERROR_CODES)tempEmg.ErrorCode)<<" Elmo err code: "<<tempEmg.ElmoErrorCode <<" Data field: "<<tempEmg.ErrorCodeDataField;
                }
            }
            catch(...)
            {
                sElmoEmergency tempEmg;
                memcpy(&tempEmg,pCommMsg->pchBuffer,(pCommMsg->DATA_LENGTH));
                rsiLOG_Oper(ERR_LEVEL)<<"GOT ELMO EL Emargency - "<<tempEmg.ErrorCode<<" Elmo err code: "<<tempEmg.ElmoErrorCode <<" Data field: "<<tempEmg.ErrorCodeDataField;
				m_sErrorReport.m_nElErrorArr[nElErrCount++ % 10] = (ELMO_ERROR_CODES)tempEmg.ErrorCode;
				m_ElmoDeviceTopicServer.ELMO_ERROR_REPORT.Publish(m_sErrorReport);
				m_sElmoDataReport.lastElElmoEmergency = tempEmg;
                if(m_sElmoDataReport.lastElElmoEmergency.ErrorCode == ELMO_ERROR_CODE_Attempt_to_access_a_non_configured_RPDO)
                {
                    m_bStepsNotAllowd = true;
                    ElmoTpdo1Mapping(EL_NODE_ID);
                    ElmoRpdo1Mapping(EL_NODE_ID);
                    m_bStepsNotAllowd = false;
                    rsiLOG_Oper(ERR_LEVEL)<<"ELMO EL RESET PDO MAPPING ";
                }
                if(!m_bPdoMappingCompleted || m_bIsInInit)
                {
                    rsiLOG_Oper(ERR_LEVEL)<<"ELMO EL - ERROR IN INITIALIZATION - RESTARTING PDO MAPPING";
                    InitAndStartMotors();
                }
                /////////////////////////////////////////
                ///////////////////////////////////////// delete this if block on ipcan!!
                if(ELMO_ERROR_CODE_CAN_message_lost_corrupted_or_overrun == (ELMO_ERROR_CODES)tempEmg.ErrorCode)
                {
                  rsiLOG(ERR_LEVEL)<<"GOT ELMO EL Emargency - "<<m_errorMap.at((ELMO_ERROR_CODES)tempEmg.ErrorCode)<<" Elmo err code: "<<tempEmg.ElmoErrorCode <<" Data field: "<<tempEmg.ErrorCodeDataField;
                }
                /////////////////////////////////////////
                ////////////////////////////////////////
                else if(IsResetAllowed((ELMO_ERROR_CODES)tempEmg.ErrorCode))
                {
                    ClearErrorsEl();
                    rsiLOG(ERR_LEVEL)<<"ELMO EL CLEAR ERRORS";
                }
                else //critical error
                {
                    m_bIsElMotorOk = false;
                    m_bIsInCriticalErrEl = true;
                    m_ElmoDeviceTopicServer.ELMO_EL_CRITICAL_ERROR.Publish(true);
                    rsiLOG_Oper(ERR_LEVEL)<<"GOT ELMO EL CRITICAL ERROR - "<<m_errorMap.at((ELMO_ERROR_CODES)tempEmg.ErrorCode)<<" Elmo err code: "<<tempEmg.ElmoErrorCode <<" Data field: "<<tempEmg.ErrorCodeDataField;
                }
            }
                break;
        }
        case ELMO_2_MDCU_TR_EMERGENCY:
        {
            try
            {
                sElmoEmergency tempEmg;
                memcpy(&tempEmg,pCommMsg->pchBuffer,(pCommMsg->DATA_LENGTH));
                rsiLOG_Oper(ERR_LEVEL)<<"GOT ELMO TR Emargency - "<<m_errorMap.at((ELMO_ERROR_CODES)tempEmg.ErrorCode)<<" Elmo err code: "<<tempEmg.ElmoErrorCode <<" Data field: "<<tempEmg.ErrorCodeDataField;
				m_sErrorReport.m_nTrErrorArr[nTrErrCount++ % 10] = (ELMO_ERROR_CODES)tempEmg.ErrorCode;
				m_ElmoDeviceTopicServer.ELMO_ERROR_REPORT.Publish(m_sErrorReport);
				m_sElmoDataReport.lastTrElmoEmergency = tempEmg;
                if(m_sElmoDataReport.lastTrElmoEmergency.ErrorCode == ELMO_ERROR_CODE_Attempt_to_access_a_non_configured_RPDO)
                {
                    m_bStepsNotAllowd = true;
                    ElmoTpdo1Mapping(TR_NODE_ID);
                    ElmoRpdo1Mapping(TR_NODE_ID);
                    m_bStepsNotAllowd = false;
                    rsiLOG(ERR_LEVEL)<<"ELMO TR RESET PDO MAPPING ";
                }
                if(!m_bPdoMappingCompleted || m_bIsInInit)
                {
                    rsiLOG(ERR_LEVEL)<<"ELMO TR - ERROR IN INITIALIZATION - RESTARTING PDO MAPPING";
                    InitAndStartMotors();
                }
                /////////////////////////////////////////
                ///////////////////////////////////////// delete this if block on ipcan!!
                if(ELMO_ERROR_CODE_CAN_message_lost_corrupted_or_overrun == (ELMO_ERROR_CODES)tempEmg.ErrorCode)
                {
                  rsiLOG(ERR_LEVEL)<<"GOT ELMO TR Emargency - "<<m_errorMap.at((ELMO_ERROR_CODES)tempEmg.ErrorCode)<<" Elmo err code: "<<tempEmg.ElmoErrorCode <<" Data field: "<<tempEmg.ErrorCodeDataField;
                }
                /////////////////////////////////////////
                ////////////////////////////////////////
                else if(IsResetAllowed((ELMO_ERROR_CODES)tempEmg.ErrorCode))
                {
                    ClearErrorsTr();
                    rsiLOG(ERR_LEVEL)<<"ELMO TR CLEAR ERRORS";
                }
                else //critical error
                {
                    m_bIsTrMotorOk = false;
                    m_bIsInCriticalErrTr = true;
                    m_ElmoDeviceTopicServer.ELMO_TR_CRITICAL_ERROR.Publish(true);
                    rsiLOG_Oper(ERR_LEVEL)<<"GOT ELMO TR CRITICAL ERROR - "<<m_errorMap.at((ELMO_ERROR_CODES)tempEmg.ErrorCode)<<" Elmo err code: "<<tempEmg.ElmoErrorCode <<" Data field: "<<tempEmg.ErrorCodeDataField;
                }

            }
            catch(...)
            {
                sElmoEmergency tempEmg;
                memcpy(&tempEmg,pCommMsg->pchBuffer,(pCommMsg->DATA_LENGTH));
                rsiLOG_Oper(ERR_LEVEL)<<"GOT ELMO TR Emargency - "<<tempEmg.ErrorCode<<" Elmo err code: "<<tempEmg.ElmoErrorCode <<" Data field: "<<tempEmg.ErrorCodeDataField;
				m_sErrorReport.m_nTrErrorArr[nTrErrCount++ %10] = (ELMO_ERROR_CODES)tempEmg.ErrorCode;
				m_ElmoDeviceTopicServer.ELMO_ERROR_REPORT.Publish(m_sErrorReport);
				m_sElmoDataReport.lastTrElmoEmergency = tempEmg;
                if(m_sElmoDataReport.lastTrElmoEmergency.ErrorCode == ELMO_ERROR_CODE_Attempt_to_access_a_non_configured_RPDO)
                {
                    m_bStepsNotAllowd = true;
                    ElmoTpdo1Mapping(TR_NODE_ID);
                    ElmoRpdo1Mapping(TR_NODE_ID);
                    m_bStepsNotAllowd = false;
                    rsiLOG(ERR_LEVEL)<<"ELMO TR RESET PDO MAPPING ";
                }
                if(!m_bPdoMappingCompleted || m_bIsInInit)
                {
                    rsiLOG(ERR_LEVEL)<<"ELMO TR - ERROR IN INITIALIZATION - RESTARTING PDO MAPPING";
                    InitAndStartMotors();
                }
                /////////////////////////////////////////
                ///////////////////////////////////////// delete this if block on ipcan!!
                if(ELMO_ERROR_CODE_CAN_message_lost_corrupted_or_overrun == (ELMO_ERROR_CODES)tempEmg.ErrorCode)
                {
                  rsiLOG(ERR_LEVEL)<<"GOT ELMO TR Emargency - "<<m_errorMap.at((ELMO_ERROR_CODES)tempEmg.ErrorCode)<<" Elmo err code: "<<tempEmg.ElmoErrorCode <<" Data field: "<<tempEmg.ErrorCodeDataField;
                }
                /////////////////////////////////////////
                ////////////////////////////////////////
                else if(IsResetAllowed((ELMO_ERROR_CODES)tempEmg.ErrorCode))
                {
                    ClearErrorsTr();
                    rsiLOG(ERR_LEVEL)<<"ELMO TR CLEAR ERRORS";
                }
                else //critical error
                {
                    m_bIsTrMotorOk = false;
                    m_bIsInCriticalErrTr = true;
                    m_ElmoDeviceTopicServer.ELMO_TR_CRITICAL_ERROR.Publish(true);
                    rsiLOG_Oper(ERR_LEVEL)<<"GOT ELMO TR CRITICAL ERROR - "<<m_errorMap.at((ELMO_ERROR_CODES)tempEmg.ErrorCode)<<" Elmo err code: "<<tempEmg.ElmoErrorCode <<" Data field: "<<tempEmg.ErrorCodeDataField;
                }

            }
            break;
        }
        case ELMO_2_MDCU_EL_TPDO1 :
            {
                memcpy(&m_ElTPDO1,pCommMsg->pchBuffer,(pCommMsg->DATA_LENGTH));
//                ELMO_SW_ENUM swResult = ParseStatusWord(m_ElTPDO1.sw);
//               if(ELMO_SW_FAULT == swResult)
//                    {
//                        m_bIsElMotorOperational = false;
//                        m_bIsElMotorOk = false;
//                        m_ElmoDeviceTopicServer.ELMO_EL_CRITICAL_ERROR.Publish(true);
//                        rsiLOG(ERR_LEVEL) <<"GOT El ELMO FAULT BIT";
//                        break;
//                    }
                ELMO_SW_ENUM swResult = ParseStatusWord(m_ElTPDO1.sw);
               if(ELMO_SW_FAULT == swResult)
               {
                  m_nElErrCycleCounter++;
                  if(m_nElErrCycleCounter >= 1000 && !m_bIsInCriticalErrEl) // if after 2 sec elmo is still in non critical err - try to clear errors
                  {
                      ClearErrorsEl();
                  }
               }
               else
               {
                   m_nElErrCycleCounter = 0;
               }

                int tempVxStatus = m_ElTPDO1.vx;
                m_flElVxStatus = (float)(((float)(2*M_PI)*(float)tempVxStatus)/ENCODER_RESOLUTION);
                m_sElmoDataReport.flElVelocityStatus = m_flElVxStatus;
                break;
            }
        case ELMO_2_MDCU_TR_TPDO1 :
            {
                memcpy(&m_TrTPDO1,pCommMsg->pchBuffer,(pCommMsg->DATA_LENGTH));
//                ELMO_SW_ENUM swResult = ParseStatusWord(m_TrTPDO1.sw);
//               if(ELMO_SW_FAULT == swResult)
//                    {
//                        m_bIsTrMotorOperational = false;
//                        m_bIsTrMotorOk = false;
//                        m_ElmoDeviceTopicServer.ELMO_TR_CRITICAL_ERROR.Publish(true);
//                        rsiLOG(ERR_LEVEL) <<"GOT TR ELMO FAULT BIT";
//                        break;
//                    }
                ELMO_SW_ENUM swResult = ParseStatusWord(m_TrTPDO1.sw);
               if(ELMO_SW_FAULT == swResult)
               {
                  m_nTrErrCycleCounter++;
                  if(m_nTrErrCycleCounter >= 1000 && !m_bIsInCriticalErrTr) // if after 2 sec elmo is still in non critical err - try to clear errors
                  {
                      ClearErrorsTr();
                  }
               }
               else
               {
                   m_nTrErrCycleCounter = 0;
               }

                int tempVxStatus = m_TrTPDO1.vx;
                m_flTrVxStatus = (float)(((float)(2*M_PI)*(float)tempVxStatus)/ENCODER_RESOLUTION);
                m_sElmoDataReport.flTrVelocityStatus = m_flTrVxStatus;

                break;
            }
        case ELMO_2_MDCU_EL_POWER_UP_COB_ID :
            {
                m_bElNmtResponse = true;
                break;
            }
        case ELMO_2_MDCU_TR_POWER_UP_COB_ID :
            {
                m_bTrNmtResponse = true;
                break;
            }
        case ELMO_2_MDCU_HEARTBEAT :
            {
//                memcpy(&m_sSYNC,pCommMsg->pchBuffer,sizeof(m_sSYNC));
//                HandleSendPeriodicDataTimerEvent(m_sSYNC.Counter);
                break;
            }
    case ELMO_2_MDCU_EL_SDO_RESPONSE:
    {
        m_pElmoResponse = (sElmo2MdcuResponse*)pCommMsg->pchBuffer;
        auto scs = m_pElmoResponse->scs;
        auto address = m_pElmoResponse->m_index;
        switch(scs)
        {
            case INIT_SDO_DOWNLOAD:
            {
//                char command[3];
//                GetCommandByAdrr(command);
//                rsiLOG(INFO_LEVEL)<<"GOT ELMO EL RESPONSE FOR "<<command<<" INDEX: " <<std::hex<<address;
                break;
            }
            case INIT_SDO_UPLOAD:
            {

                switch(address)
                {
                    case ELMO_STATUS_WORD:
                        {
                            if((m_pElmoResponse->e == 1) && (m_pElmoResponse->s == 1))
                            {
                                memcpy(&m_ElTPDO1.sw,&m_pElmoResponse->data,(4-m_pElmoResponse->n));
                            }
                            break;
                        }//ELMO_STATUS_WORD
                    case ELMO_VU:
                        {
                        if((m_pElmoResponse->e == 1) && (m_pElmoResponse->s == 1))
                        {
                            memcpy(&m_nElVuStatus,&m_pElmoResponse->data,(4-m_pElmoResponse->n));
                        }
                        break;
                        }
                    case ELMO_VX:
                        {
                        if((m_pElmoResponse->e == 1) && (m_pElmoResponse->s == 1))
                        {
                            int tempVxStatus;
                            memcpy(&tempVxStatus,&m_pElmoResponse->data,(4-m_pElmoResponse->n));
                            m_flElVxStatus = (float)(((float)(2*M_PI)*(float)tempVxStatus)/ENCODER_RESOLUTION);
                            m_sElmoDataReport.flElVelocityStatus = m_flElVxStatus;
                        }
                        break;
                       }
                case ELMO_SR:
                    {

                        if((m_pElmoResponse->e == 1) && (m_pElmoResponse->s == 1))
                        {
                            memcpy(&m_sElSrStatus,&m_pElmoResponse->data,(4-m_pElmoResponse->n));
                        }
                        break;
                     }
                case ELMO_MOTOR_RATED_CURRENT:
                    {
                        if((m_pElmoResponse->e == 1) && (m_pElmoResponse->s == 1))
                        {
                            memcpy(&m_nElMotorRatedCurrent,&m_pElmoResponse->data,(4-m_pElmoResponse->n));
                            m_sElmoDataReport.nElMotorRatedCurrent = m_nElMotorRatedCurrent;
							rsiLOG(INFO_LEVEL) << "GOT ELMO EL MOTOR RATED CURRENT: " << m_nElMotorRatedCurrent;
							if (m_nElMotorRatedCurrentExpected != m_nElMotorRatedCurrent)
							{
								m_bStepsNotAllowd = true;
								rsiLOG(ERR_LEVEL) << "ELMO EL MOTOR RATED CURRENT IS DEFFRENT THEN EXPECTED!! " << m_nElMotorRatedCurrent<<" INSTEAD OF" << m_nElMotorRatedCurrentExpected;
								m_ElmoDeviceTopicServer.ELMO_EL_CRITICAL_ERROR_MOTOR_RATED_CURRENT_DIFFRENT_FROM_EXCPECTED.Publish();
							}
                        }
                        break;
                     }
                 default:
                        {
                           rsiLOG(INFO_LEVEL)<<"GOT ELMO EL INIT SDO UPLOAD RESPONSE FOR INDEX: " <<std::hex<<address;
                        }
                }//switch(address)
               break;
            }//INIT_SDO_UPLOAD
        }//switch(scs)
        break;
    }//ELMO_2_MDCU_EL_RESPONSE

    case ELMO_2_MDCU_TR_SDO_RESPONSE:
    {
        m_pElmoResponse = (sElmo2MdcuResponse*)pCommMsg->pchBuffer;
        auto scs = m_pElmoResponse->scs;
        auto address = m_pElmoResponse->m_index;
        switch(scs)
        {
            case INIT_SDO_DOWNLOAD:
            {
//                char command[3];
//                GetCommandByAdrr(command);
//                rsiLOG(INFO_LEVEL)<<"GOT TR ELMO RESPONSE FOR "<<command<<" INDEX: " <<std::hex<<address;
                break;
            }
            case INIT_SDO_UPLOAD:
            {

                switch(address)
                {
                    case ELMO_STATUS_WORD:
                        {
                            if((m_pElmoResponse->e == 1) && (m_pElmoResponse->s == 1))
                            {
                                memcpy(&m_TrTPDO1.sw,&m_pElmoResponse->data,(4-m_pElmoResponse->n));
                            }
                            break;
                        }//ELMO_STATUS_WORD
                    case ELMO_VU:
                        {
                        if((m_pElmoResponse->e == 1) && (m_pElmoResponse->s == 1))
                        {
                            memcpy(&m_nTrVuStatus,&m_pElmoResponse->data,(4-m_pElmoResponse->n));
                        }
                        break;
                        }
                    case ELMO_VX:
                        {
                        if((m_pElmoResponse->e == 1) && (m_pElmoResponse->s == 1))
                        {
                            int tempVxStatus;
                            memcpy(&tempVxStatus,&m_pElmoResponse->data,(4-m_pElmoResponse->n));
                            m_flTrVxStatus = (float)(((float)(2*M_PI)*(float)tempVxStatus)/ENCODER_RESOLUTION);
                            m_sElmoDataReport.flTrVelocityStatus = m_flTrVxStatus;
                        }
                        break;
                        }
                        case ELMO_SR:
                            {
                                if((m_pElmoResponse->e == 1) && (m_pElmoResponse->s == 1))
                                {
                                    memcpy(&m_sTrSrStatus,&m_pElmoResponse->data,(4-m_pElmoResponse->n));
                                }
                                break;
                             }
                        case ELMO_MOTOR_RATED_CURRENT:
                            {
                                if((m_pElmoResponse->e == 1) && (m_pElmoResponse->s == 1))
                                {
                                    memcpy(&m_nTrMotorRatedCurrent,&m_pElmoResponse->data,(4-m_pElmoResponse->n));
                                    m_sElmoDataReport.nTrMotorRatedCurrent =  m_nTrMotorRatedCurrent;
									rsiLOG(INFO_LEVEL) << "GOT ELMO TR MOTOR RATED CURRENT: " << m_nTrMotorRatedCurrent;
									if (m_nTrMotorRatedCurrentExpected != m_nTrMotorRatedCurrent)
									{
										m_bStepsNotAllowd = true;
										rsiLOG(ERR_LEVEL) << "ELMO TR MOTOR RATED CURRENT IS DEFFRENT THEN EXPECTED!! " << m_nTrMotorRatedCurrent << " INSTEAD OF" << m_nTrMotorRatedCurrentExpected;
										m_ElmoDeviceTopicServer.ELMO_TR_CRITICAL_ERROR_MOTOR_RATED_CURRENT_DIFFRENT_FROM_EXCPECTED.Publish();
									}
                                }
                                break;
                             }
                        default:
                        {
                           rsiLOG(INFO_LEVEL)<<"GOT ELMO TR INIT SDO UPLOAD RESPONSE FOR INDEX: " <<std::hex<<address;
                        }
                }//switch(address)

            }//INIT_SDO_UPLOAD
            break;
        }//switch(scs)
        break;
    }//ELMO_2_MDCU_TR_RESPONSE


        default :
            {
            return;
            }
        } //switch
}

void ElmoCanbusDevice::SendCommMsg(void * pCmd,  ELMO_COB_ID COB_ID, unsigned int nDataSize, int RTR)
{
	if (m_objectDisable)
	{
		return;
    }

    CanbusAdapterFramer::CanbusAdapterMessage canAdapterMsg;
	canAdapterMsg.header.opcode			 = CanbusAdapterFramer::Comx2Arm_ECanbusAdapterMsgOpcodes_Normal;
    canAdapterMsg.canMsg.COB_ID          = COB_ID;
    canAdapterMsg.canMsg.RTR             = RTR;
    canAdapterMsg.canMsg.DATA_LENGTH     = nDataSize;
    int messageLength = nDataSize + CANOPEN_HEADER; // The size includes the header

    if(messageLength <= CANOPEN_MESSAGE_SIZE)
    {
        memcpy(&canAdapterMsg.canMsg.pchBuffer, pCmd, nDataSize);

        const uint8* pDataToSend = (const uint8*)canAdapterMsg.canMsg.pchBuffer;
        if(m_bRemoteCanbus)
        {
            pDataToSend = (const uint8*)&canAdapterMsg;
            messageLength = sizeof(canAdapterMsg);
        }

        if (!m_pCommChannel->outgoing.Publish((const uint8_t*)pDataToSend, messageLength))
        {
            rsiLOG(WARNING_LEVEL) << "Unable to send msg to comm channel";
        }

        m_sendTimeTag = rsiTime::currentSystemTimeMicros();
    }
    else
    {
        rsiLOG(WARNING_LEVEL)
                << "Comm message size is too large ["
                << messageLength << ">"
                << CANOPEN_MESSAGE_SIZE - 1<<"]";
    }

}

void ElmoCanbusDevice::SendCommMsg(void * pCmd,  ELMO_COB_ID COB_ID,int RTR)
{
    SendCommMsg(pCmd,COB_ID,CANOPEN_DATA_SIZE,RTR);
}

void ElmoCanbusDevice::MaxContinuousCurrentRequest(ELMO_NODE_ID nodeId)
{
     ELMO_COB_ID SDO_COB_ID = (nodeId == EL_NODE_ID ? MDCU_2_ELMO_ELEVATION_SDO_COB_ID : MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
     sMdcu2ElmoInitUpload cmd;
     cmd.css =  0x02;
     cmd.x =    0x00;
     cmd.m =    0x6075;
     cmd.reserved = 0x00;
     SendCommMsg(&cmd,SDO_COB_ID);
}

void ElmoCanbusDevice::SendTorqueProfileMode(ELMO_NODE_ID nodeId)
{
     ELMO_COB_ID SDO_COB_ID = (nodeId == EL_NODE_ID ? MDCU_2_ELMO_ELEVATION_SDO_COB_ID : MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);

     SendCommMsg(&m_TorqueProfileMode,SDO_COB_ID);
}


void ElmoCanbusDevice::SetElMotorOn()
{
    m_pTopicDispatcher->StartTimer(m_ElStartMotorTimerId);
}

void ElmoCanbusDevice::SetTrMotorOn()
{
    m_pTopicDispatcher->StartTimer(m_TrStartMotorTimerId);
}

void ElmoCanbusDevice::Step()
{
    if (m_objectDisable || m_bStepsNotAllowd || m_bIsInInit || !m_bIsElMotorOk || !m_bIsTrMotorOk || !m_bPdoMappingCompleted)
    {
        return;
    }

    if(m_bDebug)
    {
        m_ElRPDO1.targetTorque = CalcElTargetTorque(5.0f);
        m_TrRPDO1.targetTorque = CalcTrTargetTorque(5.0f);
    }

	// send all 3 step messages combined...
	memcpy(m_StepMessage.canMsg[CanbusAdapterFramer::EStepMessages_ElRPDO1].pchBuffer, &m_ElRPDO1, sizeof(m_ElRPDO1));
	memcpy(m_StepMessage.canMsg[CanbusAdapterFramer::EStepMessages_TrRPDO1].pchBuffer, &m_TrRPDO1, sizeof(m_TrRPDO1));
	memcpy(m_StepMessage.canMsg[CanbusAdapterFramer::EStepMessages_Sync].pchBuffer, &m_sync, sizeof(m_sync));
	
    if (!m_pCommChannel->outgoing.Publish((const uint8_t*)&m_StepMessage, sizeof(m_StepMessage)))
    {
        rsiLOG(WARNING_LEVEL) << "Unable to send msg to comm channel";
    }

	m_sendTimeTag = rsiTime::currentSystemTimeMicros();
}

void ElmoCanbusDevice::DoElmoPdoMapping()
{
    m_pTopicDispatcher->StartTimer(m_ElmoElTpdo1MappingTimerId);
}

void ElmoCanbusDevice::ElmoTpdo1Mapping(ELMO_NODE_ID nodeId)
{
#ifndef WIN32
usleep(100);
    ELMO_COB_ID SDO_COB_ID = (nodeId == EL_NODE_ID ? MDCU_2_ELMO_ELEVATION_SDO_COB_ID : MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
    sElmoGeneralCommand cmd;
    //Disable TPDO1 cobid 0x180 + nodeId
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x18;
    cmd.byte3 = 0x01;
    cmd.byte4 = 0x80 + nodeId;
    cmd.byte5 = 0x01;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x80; //set not valid
    SendCommMsg(&cmd,SDO_COB_ID);

usleep(100);
    //clear mapping TPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x1A;
    cmd.byte3 = 0x00;
    cmd.byte4 = 0x00;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,SDO_COB_ID);

usleep(100);
    //Set VX, object 0x6069 ,32bit len to sub index 1 of TPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x1A;
    cmd.byte3 = 0x01;//sub index
    cmd.byte4 = 0x20;//len
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x69;
    cmd.byte7 = 0x60;
    SendCommMsg(&cmd,SDO_COB_ID);

//usleep(10);
//    //Set SR, object 0x1002 ,32bit len to sub index 2 of TPDO1
//    cmd.byte0 = 0x22;
//    cmd.byte1 = 0x00;
//    cmd.byte2 = 0x1A;
//    cmd.byte3 = 0x02;//sub index
//    cmd.byte4 = 0x20;//len
//    cmd.byte5 = 0x00;
//    cmd.byte6 = 0x02;
//    cmd.byte7 = 0x10;
//    SendCommMsg(&cmd,SDO_COB_ID);

usleep(100);
    //Set SW, object 0x6041 ,16bit len to sub index 2 of TPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x1A;
    cmd.byte3 = 0x02;//sub index
    cmd.byte4 = 0x10;//len
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x41;
    cmd.byte7 = 0x60;
    SendCommMsg(&cmd,SDO_COB_ID);

	usleep(100);
	//Set Current Actual Value, object 0x6078 ,16bit len to sub index 3 of TPDO1
	cmd.byte0 = 0x22;
	cmd.byte1 = 0x00;
	cmd.byte2 = 0x1A;
	cmd.byte3 = 0x03;//sub index
	cmd.byte4 = 0x10;//len
	cmd.byte5 = 0x00;
	cmd.byte6 = 0x78;
	cmd.byte7 = 0x60;
	SendCommMsg(&cmd, SDO_COB_ID);

usleep(100);
    //set 3 objects are mapped
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x1A;
    cmd.byte3 = 0x00;
    cmd.byte4 = 0x03;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,SDO_COB_ID);

usleep(100);
    //Set Transmition type - every Sync
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x18;
    cmd.byte3 = 0x02;
    cmd.byte4 = 0x01;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,SDO_COB_ID);

usleep(100);
    //Enable Tpdo1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x18;
    cmd.byte3 = 0x01;
    cmd.byte4 = 0x80 + nodeId;
    cmd.byte5 = 0x01;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,SDO_COB_ID);
#endif
}

void ElmoCanbusDevice::ElmoRpdo1Mapping(ELMO_NODE_ID nodeId)
{
#ifndef WIN32
usleep(100);
    ELMO_COB_ID SDO_COB_ID = (nodeId == EL_NODE_ID ? MDCU_2_ELMO_ELEVATION_SDO_COB_ID : MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
    sElmoGeneralCommand cmd;
    //Disable RPDO1 cobid 0x200 + nodeId
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x14;
    cmd.byte3 = 0x01;
    cmd.byte4 = 0x00 + nodeId;
    cmd.byte5 = 0x02;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x80; //set not valid
    SendCommMsg(&cmd,SDO_COB_ID);

usleep(100);
    //clear mapping RPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x16;
    cmd.byte3 = 0x00;
    cmd.byte4 = 0x00;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x80;
    SendCommMsg(&cmd,SDO_COB_ID);

usleep(100);
    //Set CW object 0x6040 , 16 bit len to sub index 1 of RPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x16;
    cmd.byte3 = 0x01;
    cmd.byte4 = 0x10;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x40;
    cmd.byte7 = 0x60;
    SendCommMsg(&cmd,SDO_COB_ID);

usleep(100);
    //Set Target Torque object 0x6071 , 16 bit len to sub index 2 of RPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x16;
    cmd.byte3 = 0x02;
    cmd.byte4 = 0x10;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x71;
    cmd.byte7 = 0x60;
    SendCommMsg(&cmd,SDO_COB_ID);

usleep(100);
    //Set transmition type - every Sync
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x14;
    cmd.byte3 = 0x02;
    cmd.byte4 = 0x01;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,SDO_COB_ID);

usleep(100);
    //set 2 objects are mapped
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x16;
    cmd.byte3 = 0x00;
    cmd.byte4 = 0x02;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,SDO_COB_ID);

usleep(100);
    //Enable RPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x14;
    cmd.byte3 = 0x01;
    cmd.byte4 = 0x00 + nodeId;
    cmd.byte5 = 0x02;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,SDO_COB_ID);
#endif
}

////////////////////////////////////////
void ElmoCanbusDevice::ElmoElTpdo1Mapping()
{

    sElmoGeneralCommand cmd;
    static unsigned short cycle= 0;
   if(0 ==cycle){
    //Disable TPDO1 cobid 0x180 + nodeId
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x18;
    cmd.byte3 = 0x01;
    cmd.byte4 = 0x80 + EL_NODE_ID;
    cmd.byte5 = 0x01;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x80; //set not valid
    SendCommMsg(&cmd,MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
    }
else if(1 == cycle )
   {
    //clear mapping TPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x1A;
    cmd.byte3 = 0x00;
    cmd.byte4 = 0x00;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
}
   else if(2 == cycle )
      {
    //Set VX, object 0x6069 ,32bit len to sub index 1 of TPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x1A;
    cmd.byte3 = 0x01;//sub index
    cmd.byte4 = 0x20;//len
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x69;
    cmd.byte7 = 0x60;
    SendCommMsg(&cmd,MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
}
//usleep(10);
//    //Set SR, object 0x1002 ,32bit len to sub index 2 of TPDO1
//    cmd.byte0 = 0x22;
//    cmd.byte1 = 0x00;
//    cmd.byte2 = 0x1A;
//    cmd.byte3 = 0x02;//sub index
//    cmd.byte4 = 0x20;//len
//    cmd.byte5 = 0x00;
//    cmd.byte6 = 0x02;
//    cmd.byte7 = 0x10;
//    SendCommMsg(&cmd,SDO_COB_ID);

   else if(3 == cycle )
      {
    //Set SW, object 0x6041 ,16bit len to sub index 2 of TPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x1A;
    cmd.byte3 = 0x02;//sub index
    cmd.byte4 = 0x10;//len
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x41;
    cmd.byte7 = 0x60;
    SendCommMsg(&cmd,MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
}

   else if(4 == cycle )
      {
    //Set Current Actual Value, object 0x6078 ,16bit len to sub index 3 of TPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x1A;
    cmd.byte3 = 0x03;//sub index
    cmd.byte4 = 0x10;//len
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x78;
    cmd.byte7 = 0x60;
    SendCommMsg(&cmd, MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
}
   else if(5 == cycle )
      {
    //set 3 objects are mapped
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x1A;
    cmd.byte3 = 0x00;
    cmd.byte4 = 0x03;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
}
   else if(6 == cycle )
      {
    //Set Transmition type - every Sync
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x18;
    cmd.byte3 = 0x02;
    cmd.byte4 = 0x01;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
}
   else if(7 == cycle )
      {
    //Enable Tpdo1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x18;
    cmd.byte3 = 0x01;
    cmd.byte4 = 0x80 + EL_NODE_ID;
    cmd.byte5 = 0x01;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
}
   else
   {
       //stop timer
       m_pTopicDispatcher->StopTimer(m_ElmoElTpdo1MappingTimerId);
       cycle = 0;
       m_pTopicDispatcher->StartTimer(m_ElmoElRpdo1MappingTimerId);
       return;
   }
   ++cycle;
}

void ElmoCanbusDevice::ElmoElRpdo1Mapping()
{

    sElmoGeneralCommand cmd;
    static unsigned short cycle= 0;
  if(0 == cycle){
    //Disable RPDO1 cobid 0x200 + nodeId
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x14;
    cmd.byte3 = 0x01;
    cmd.byte4 = 0x00 + EL_NODE_ID;
    cmd.byte5 = 0x02;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x80; //set not valid
    SendCommMsg(&cmd,MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
    }
 else if(1 == cycle){

    //clear mapping RPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x16;
    cmd.byte3 = 0x00;
    cmd.byte4 = 0x00;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x80;
    SendCommMsg(&cmd,MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
}
  else if(2 == cycle){

    //Set CW object 0x6040 , 16 bit len to sub index 1 of RPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x16;
    cmd.byte3 = 0x01;
    cmd.byte4 = 0x10;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x40;
    cmd.byte7 = 0x60;
    SendCommMsg(&cmd,MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
}
   else if(3 == cycle){
    //Set Target Torque object 0x6071 , 16 bit len to sub index 2 of RPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x16;
    cmd.byte3 = 0x02;
    cmd.byte4 = 0x10;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x71;
    cmd.byte7 = 0x60;
    SendCommMsg(&cmd,MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
}
else if(4 == cycle){
    //Set transmition type - every Sync
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x14;
    cmd.byte3 = 0x02;
    cmd.byte4 = 0x01;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
}
else if(5 == cycle){
    //set 2 objects are mapped
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x16;
    cmd.byte3 = 0x00;
    cmd.byte4 = 0x02;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
}
else if(6 == cycle){
    //Enable RPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x14;
    cmd.byte3 = 0x01;
    cmd.byte4 = 0x00 + EL_NODE_ID;
    cmd.byte5 = 0x02;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
}
   else
   {
    m_pTopicDispatcher->StopTimer(m_ElmoElRpdo1MappingTimerId);
    cycle = 0;
    m_pTopicDispatcher->StartTimer(m_ElmoTrTpdo1MappingTimerId);
    return;
   }
   ++cycle;
}

//
//
void ElmoCanbusDevice::ElmoTrTpdo1Mapping()
{

    sElmoGeneralCommand cmd;
    static unsigned short cycle= 0;
   if(0 ==cycle){
    //Disable TPDO1 cobid 0x180 + nodeId
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x18;
    cmd.byte3 = 0x01;
    cmd.byte4 = 0x80 + TR_NODE_ID;
    cmd.byte5 = 0x01;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x80; //set not valid
    SendCommMsg(&cmd,MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
    }
else if(1 == cycle )
   {
    //clear mapping TPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x1A;
    cmd.byte3 = 0x00;
    cmd.byte4 = 0x00;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
}
   else if(2 == cycle )
      {
    //Set VX, object 0x6069 ,32bit len to sub index 1 of TPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x1A;
    cmd.byte3 = 0x01;//sub index
    cmd.byte4 = 0x20;//len
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x69;
    cmd.byte7 = 0x60;
    SendCommMsg(&cmd,MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
}
//usleep(10);
//    //Set SR, object 0x1002 ,32bit len to sub index 2 of TPDO1
//    cmd.byte0 = 0x22;
//    cmd.byte1 = 0x00;
//    cmd.byte2 = 0x1A;
//    cmd.byte3 = 0x02;//sub index
//    cmd.byte4 = 0x20;//len
//    cmd.byte5 = 0x00;
//    cmd.byte6 = 0x02;
//    cmd.byte7 = 0x10;
//    SendCommMsg(&cmd,SDO_COB_ID);

   else if(3 == cycle )
      {
    //Set SW, object 0x6041 ,16bit len to sub index 2 of TPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x1A;
    cmd.byte3 = 0x02;//sub index
    cmd.byte4 = 0x10;//len
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x41;
    cmd.byte7 = 0x60;
    SendCommMsg(&cmd,MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
}

   else if(4 == cycle )
      {
    //Set Current Actual Value, object 0x6078 ,16bit len to sub index 3 of TPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x1A;
    cmd.byte3 = 0x03;//sub index
    cmd.byte4 = 0x10;//len
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x78;
    cmd.byte7 = 0x60;
    SendCommMsg(&cmd, MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
}
   else if(5 == cycle )
      {
    //set 3 objects are mapped
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x1A;
    cmd.byte3 = 0x00;
    cmd.byte4 = 0x03;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
}
   else if(6 == cycle )
      {
    //Set Transmition type - every Sync
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x18;
    cmd.byte3 = 0x02;
    cmd.byte4 = 0x01;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
}
   else if(7 == cycle )
      {
    //Enable Tpdo1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x18;
    cmd.byte3 = 0x01;
    cmd.byte4 = 0x80 + TR_NODE_ID;
    cmd.byte5 = 0x01;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
}
   else
   {
       //stop timer
       m_pTopicDispatcher->StopTimer(m_ElmoTrTpdo1MappingTimerId);
       cycle = 0;
       m_pTopicDispatcher->StartTimer(m_ElmoTrRpdo1MappingTimerId);
       return;
   }
   ++cycle;
}

void ElmoCanbusDevice::ElmoTrRpdo1Mapping()
{

    sElmoGeneralCommand cmd;
    static unsigned short cycle= 0;
  if(0 == cycle){
    //Disable RPDO1 cobid 0x200 + nodeId
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x14;
    cmd.byte3 = 0x01;
    cmd.byte4 = 0x00 + TR_NODE_ID;
    cmd.byte5 = 0x02;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x80; //set not valid
    SendCommMsg(&cmd,MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
    }
 else if(1 == cycle){

    //clear mapping RPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x16;
    cmd.byte3 = 0x00;
    cmd.byte4 = 0x00;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x80;
    SendCommMsg(&cmd,MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
}
  else if(2 == cycle){

    //Set CW object 0x6040 , 16 bit len to sub index 1 of RPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x16;
    cmd.byte3 = 0x01;
    cmd.byte4 = 0x10;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x40;
    cmd.byte7 = 0x60;
    SendCommMsg(&cmd,MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
}
   else if(3 == cycle){
    //Set Target Torque object 0x6071 , 16 bit len to sub index 2 of RPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x16;
    cmd.byte3 = 0x02;
    cmd.byte4 = 0x10;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x71;
    cmd.byte7 = 0x60;
    SendCommMsg(&cmd,MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
}
else if(4 == cycle){
    //Set transmition type - every Sync
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x14;
    cmd.byte3 = 0x02;
    cmd.byte4 = 0x01;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
}
else if(5 == cycle){
    //set 2 objects are mapped
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x16;
    cmd.byte3 = 0x00;
    cmd.byte4 = 0x02;
    cmd.byte5 = 0x00;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
}
else if(6 == cycle){
    //Enable RPDO1
    cmd.byte0 = 0x22;
    cmd.byte1 = 0x00;
    cmd.byte2 = 0x14;
    cmd.byte3 = 0x01;
    cmd.byte4 = 0x00 + TR_NODE_ID;
    cmd.byte5 = 0x02;
    cmd.byte6 = 0x00;
    cmd.byte7 = 0x00;
    SendCommMsg(&cmd,MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
}
   else
   {
       //stop timer
    m_pTopicDispatcher->StopTimer(m_ElmoTrRpdo1MappingTimerId);
    cycle = 0;
    m_bPdoMappingCompleted = true;

#ifndef WIN32
//	usleep(1000);
#endif

    return;
   }
   ++cycle;
}

//////////////////////////////////////////

void ElmoCanbusDevice::SendDisableTPDO1Command()
{
	SendCommMsg(&m_sDisableTPDO1,MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
	SendCommMsg(&m_sDisableTPDO1,MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
}

void ElmoCanbusDevice::SendElMotorEnableDisableCommand(bool onOff)
{
    if (m_objectDisable || m_bDebug || m_bIsInInit)
    {
        return;
    }
    if(true == onOff)
    {
        ELMO_SW_ENUM result = ParseStatusWord(m_ElTPDO1.sw);
        if(result == ELMO_SW_MOTOR_ON)
        {
           m_ElRPDO1.cw.enableOperation = onOff;
           m_bIsElMotorOperational = false;
        }
        else if(result == ELMO_SW_MOTOR_OPERATIONAL)
        {
            m_ElRPDO1.cw.enableOperation = onOff;
            m_bIsElMotorOperational = true;
        }
        else
        {
            if(m_bIsElMotorOk && !m_bIsInCriticalErrEl)
            {
                m_bIsElMotorOk = false;
                m_bIsElMotorOperational = false;
                SetElMotorOn();
            }
        }
    }
    else
    {
        m_bIsElMotorOperational = false;
        m_ElRPDO1.cw.enableOperation = onOff;
    }


}

void ElmoCanbusDevice::SendTrMotorEnableDisableCommand(bool onOff)
{
    if(true == onOff)
    {
        ELMO_SW_ENUM result = ParseStatusWord(m_TrTPDO1.sw);
        if(result == ELMO_SW_MOTOR_ON)
        {
           m_TrRPDO1.cw.enableOperation = onOff;
           m_bIsTrMotorOperational = false;
        }
        else if(result == ELMO_SW_MOTOR_OPERATIONAL)
        {
            m_TrRPDO1.cw.enableOperation = onOff;
            m_bIsTrMotorOperational = true;
        }
        else
        {
           if(m_bIsTrMotorOk && !m_bIsInCriticalErrTr)
           {
               m_bIsTrMotorOk = false;
               m_bIsTrMotorOperational = false;
               SetTrMotorOn();
           }
        }

    }
    else
    {
         m_TrRPDO1.cw.enableOperation = onOff;
         m_bIsTrMotorOperational = false;
    }
}

void ElmoCanbusDevice::SendElTcCommand(float amp)
{
    if (m_objectDisable || m_bDebug || m_bIsInInit)
	{
		return;
	}
    m_sElmoDataReport.flElAmpControlAlg = amp;
    if(ELMO_SW_MOTOR_OPERATIONAL == ParseStatusWord(m_ElTPDO1.sw))
    {
        m_ElRPDO1.targetTorque = CalcElTargetTorque(amp);
    }
    else
    {
      m_ElRPDO1.targetTorque = 0;
    }
    m_sElmoDataReport.shElTargetTorqueToElmo =  m_ElRPDO1.targetTorque;
    m_sElmoDataReport.fElAmpCommand = amp;
}

void ElmoCanbusDevice::SendTrTcCommand(float amp)
{
    if (m_objectDisable || m_bDebug || m_bIsInInit)
    {
        return;
    }
    m_sElmoDataReport.flTrAmpControlAlg = amp;
    if(ELMO_SW_MOTOR_OPERATIONAL == ParseStatusWord(m_TrTPDO1.sw))
    {
		m_TrRPDO1.targetTorque = CalcTrTargetTorque(amp);
    }
    else
    {
      m_TrRPDO1.targetTorque = 0;
    }
    m_sElmoDataReport.shTrTargetTorqueToElmo =  m_TrRPDO1.targetTorque;
    m_sElmoDataReport.fTrAmpCommand = amp;
}

void ElmoCanbusDevice::SendElStopProfilerCommand()
{
	if (m_objectDisable)
	{
		return;
	}

	SendCommMsg(&m_sStCommand,MDCU_2_ELMO_ELEVATION_SDO_COB_ID );
}

void ElmoCanbusDevice::SendTrStopProfilerCommand()
{
	if (m_objectDisable)
	{
		return;
	}

	SendCommMsg(&m_sStCommand, MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
}

void ElmoCanbusDevice::SendElUmCommand(UM_ENUM cmd)
{
	if (m_objectDisable)
	{
		return;
	}

    m_sUmCommand.nData = cmd;
	SendCommMsg(&m_sUmCommand, MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
}

void ElmoCanbusDevice::SendTrUmCommand(UM_ENUM cmd)
{
	if (m_objectDisable)
	{
		return;
	}

    m_sUmCommand.nData = cmd;
	SendCommMsg(&m_sUmCommand, MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
}

void ElmoCanbusDevice::SendElTorqueSlopeCommand()
{
    if (m_objectDisable)
    {
        return;
    }

    SendCommMsg(&m_sElTorqueSlope, MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
}

void ElmoCanbusDevice::SendTrTorqueSlopeCommand()
{
	if (m_objectDisable)
	{
		return;
	}

	SendCommMsg(&m_sTrTorqueSlope, MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
}

void ElmoCanbusDevice::SendElSWRequest()
{
	SendCommMsg(&m_sStatusWordRequest, MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
}

void ElmoCanbusDevice::SendTrSWRequest()
{
	SendCommMsg(&m_sStatusWordRequest, MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
}


void ElmoCanbusDevice::SendElVuRequest()
{
	SendCommMsg(&m_sVuRequest, MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
}

void ElmoCanbusDevice::SendTrVuRequest()
{
	SendCommMsg(&m_sVuRequest, MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
}

void ElmoCanbusDevice::SendElVxRequest()
{
	SendCommMsg(&m_sVxRequest, MDCU_2_ELMO_ELEVATION_SDO_COB_ID);
}

void ElmoCanbusDevice::SendTrVxRequest()
{
	SendCommMsg(&m_sVxRequest, MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID);
}

void ElmoCanbusDevice::SendElSrRequest()
{
    return;
//		SendCommMsg(&m_sSrRequest, MDCU_2_ELMO_ELEVATION_COB_ID);
}

void ElmoCanbusDevice::SendTrSrRequest()
{
    return;
//		SendCommMsg(&m_sSrRequest, MDCU_2_ELMO_TRANSVERSE_COB_ID);
}

int ElmoCanbusDevice::GetElVuStatus()
{
    return m_nElVuStatus;
}

int ElmoCanbusDevice::GetTrVuStatus()
{
	return m_nTrVuStatus;
}

float ElmoCanbusDevice::GetElVxStatus()
{
     return m_flElVxStatus;
}

float ElmoCanbusDevice::GetTrVxStatus()
{
   return m_flTrVxStatus;
}

bool ElmoCanbusDevice::GetElAmplifierActivateStatus()
{
    return m_bIsElMotorOperational ;
}

bool ElmoCanbusDevice::GetTrAmplifierActivateStatus()
{
    return m_bIsTrMotorOperational ;
}

float ElmoCanbusDevice::GetElMotorEnabled()
{
    return m_ElTPDO1.sw.operationEnabled;
}

float ElmoCanbusDevice::GetTrMotorEnabled()
{
    return m_TrTPDO1.sw.operationEnabled;
}

void ElmoCanbusDevice::SendTelemetryData()
{
    memcpy(&m_sElmoDataReport.shElControlWord,&m_ElRPDO1.cw,sizeof(short));
    memcpy(&m_sElmoDataReport.shTrControlWord,&m_TrRPDO1.cw,sizeof(short));
    m_ElmoDeviceTopicServer.ELMO_TELEMETRY_REPORT.Publish(m_sElmoDataReport);
}

void ElmoCanbusDevice::Debug(float num)
{
    if(num ==0)
    {
        m_bDebug = true;
        m_pTopicDispatcher->StartTimer(m_debugTimerId);

    }
    if(num ==1)
    {
        m_bDebug = true;
         m_ElRPDO1.targetTorque = CalcElTargetTorque(5);
         m_ElRPDO1.cw.enableOperation = true;
         m_bIsElMotorOperational = true;
         std::cout<<"el start"<<std::endl;

    }
    else if(num == 2)
    {
        m_bDebug = true;
        m_TrRPDO1.targetTorque = CalcTrTargetTorque(5);
        m_TrRPDO1.cw.enableOperation = true;
        m_bIsTrMotorOperational = true;
         std::cout<<"tr start"<<std::endl;
    }
    if(num == 3)
    {
         m_ElRPDO1.targetTorque = 0;
         m_ElRPDO1.cw.enableOperation = false;
         m_bIsElMotorOperational = false;
         std::cout<<"el stop"<<std::endl;

    }
    else if(num == 4)
    {
        m_TrRPDO1.targetTorque = 0;
        m_TrRPDO1.cw.enableOperation = false;
        m_bIsTrMotorOperational = false;
        std::cout<<"Tr stop"<<std::endl;
    }
}

void ElmoCanbusDevice::SendNmtStartCommand(int nodeId)
{
    m_sNMTModuleControl.CommandSpecifier = NMT_START_COMMAND;
    m_sNMTModuleControl.NodeID = nodeId;
    SendCommMsg(&m_sNMTModuleControl,MDCU_2_ELMO_NMT_MODULE_CONTROL_COB_ID,sizeof(m_sNMTModuleControl),0);
}

void ElmoCanbusDevice::SendNmtNodeResetCommand(int nodeId)
{
    m_sNMTModuleControl.CommandSpecifier = NMT_RESET_NODE;
    m_sNMTModuleControl.NodeID = nodeId;
    SendCommMsg(&m_sNMTModuleControl,MDCU_2_ELMO_NMT_MODULE_CONTROL_COB_ID,sizeof(m_sNMTModuleControl),0);
}

void ElmoCanbusDevice::SendNmtResetCommCommand(int nodeId)
{
    m_sNMTModuleControl.CommandSpecifier = NMT_RESET_COMMUNICATION;
    m_sNMTModuleControl.NodeID = nodeId;
    SendCommMsg(&m_sNMTModuleControl,MDCU_2_ELMO_NMT_MODULE_CONTROL_COB_ID,sizeof(m_sNMTModuleControl),0);
}

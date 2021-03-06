/**
 * @file atProxy.c
 *
 * AT Proxy interface implementation.
 *
 * Copyright (C) Sierra Wireless Inc.
 */

#include "legato.h"
#include "interfaces.h"
#include "atProxy.h"
#include "atProxyCmdHandler.h"
#include "atProxySerialUart.h"
#include "atProxyAdaptor.h"

#define AT_PROXY_CMD_REGISTRY_IMPL   1
#include "atProxyCmdRegistry.h"

//--------------------------------------------------------------------------------------------------
/**
 * A magic number used to convert between command index and reference
 * This macro is temporarily needed to adapt static command registration to le_atServer.api, and
 * can be removed when change le_atServer_AddCommandHandler() in le_atServer.api to use command
 * index instead of reference.
 */
//--------------------------------------------------------------------------------------------------
#define AT_PROXY_CMD_MAGIC_NUMBER   0xF0000001

//--------------------------------------------------------------------------------------------------
/**
 * Convert command index to reference
 *
 * This macro is temporarily needed to adapt static command registration to le_atServer.api, and
 * can be removed when change le_atServer_AddCommandHandler() in le_atServer.api to use command
 * index instead of reference.
 */
//--------------------------------------------------------------------------------------------------
#define AT_PROXY_CONVERT_IND2REF(ind)   \
    ((le_atServer_CmdRef_t)(((ind) << 1) | AT_PROXY_CMD_MAGIC_NUMBER))


//--------------------------------------------------------------------------------------------------
/**
 * Convert command reference to index
 *
 * This macro is temporarily needed to adapt static command registration to le_atServer.api, and
 * can be removed when change le_atServer_AddCommandHandler() in le_atServer.api to use command
 * index instead of reference.
 */
//--------------------------------------------------------------------------------------------------
#define AT_PROXY_CONVERT_REF2IND(ref)    \
    (((uint32_t)(ref) & (~AT_PROXY_CMD_MAGIC_NUMBER)) >> 1)

//--------------------------------------------------------------------------------------------------
/**
 * Static map for AT Command references
 */
//--------------------------------------------------------------------------------------------------
LE_REF_DEFINE_STATIC_MAP(AtCmdRefMap, AT_CMD_MAX);

//--------------------------------------------------------------------------------------------------
/**
 * Map for AT commands
 */
//--------------------------------------------------------------------------------------------------
static le_ref_MapRef_t  AtCmdRefMap;


//--------------------------------------------------------------------------------------------------
/**
 * Map for AT Command Sessions
 */
//--------------------------------------------------------------------------------------------------
extern le_ref_MapRef_t  atCmdSessionRefMap;


//--------------------------------------------------------------------------------------------------
/**
 * Function to retrieve the AT Command Registry
 */
//--------------------------------------------------------------------------------------------------
struct le_atProxy_StaticCommand* le_atProxy_GetCmdRegistry
(
    void
)
{
    return &AtCmdRegistry[0];
}

//--------------------------------------------------------------------------------------------------
/**
 * Function to retrieve the AT Command Registry entry for a specific command
 */
//--------------------------------------------------------------------------------------------------
struct le_atProxy_StaticCommand* le_atProxy_GetCmdRegistryEntry
(
    uint32_t command
)
{
    // Verify the command index is within range
    LE_ASSERT(command < AT_CMD_MAX);

    return &AtCmdRegistry[command];
}


//--------------------------------------------------------------------------------------------------
/**
 * Add handler function for EVENT 'le_atProxy_Command'
 *
 * This event provides information when the AT command is detected.
 *
 */
//--------------------------------------------------------------------------------------------------
le_atServer_CommandHandlerRef_t le_atServer_AddCommandHandler
(
    le_atServer_CmdRef_t cmdRef,
        ///< [IN] AT Command index
    le_atServer_CommandHandlerFunc_t handlerPtr,
        ///< [IN] Handler to called when the AT command is detected
    void* contextPtr
        ///< [IN] Context pointer provided by caller and returned when handlePtr is called
)
{
    LE_DEBUG("Calling le_atProxy_AddCommandHandler");

    uint32_t command = AT_PROXY_CONVERT_REF2IND(cmdRef);

    if (NULL == cmdRef || command >= AT_CMD_MAX)
    {
        return NULL;
    }

    // Set pointer to AT Command Register entry
    struct le_atProxy_StaticCommand* atCmdRegistryPtr = &AtCmdRegistry[command];

    // Set the Command Handler Callback function and Context Pointer
    atCmdRegistryPtr->commandHandlerPtr = handlerPtr;
    atCmdRegistryPtr->contextPtr = contextPtr;

    // Create Safe Reference to AT Command Registry entry
    return le_ref_CreateRef(AtCmdRefMap, atCmdRegistryPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Remove handler function for EVENT 'le_atProxy_Command'
 */
//--------------------------------------------------------------------------------------------------
void le_atServer_RemoveCommandHandler
(
    le_atServer_CommandHandlerRef_t handlerRef
        ///< [IN] Reference of Command Handler to be removed
)
{
    // Look-up AT Command Register entry using handlerRef
    struct le_atProxy_StaticCommand* atCmdRegistryPtr = le_ref_Lookup(AtCmdRefMap, handlerRef);

    // Delete Safe Reference to AT Command Registry entry
    le_ref_DeleteRef(AtCmdRefMap, handlerRef);

    if (atCmdRegistryPtr == NULL)
    {
        LE_INFO("Unable to retrieve AT Command Registry entry, handlerRef [%" PRIuPTR "]",
                (uintptr_t) handlerRef);
        return;
    }

    // Reset the Command Handler Callback function and Context Pointer
    atCmdRegistryPtr->commandHandlerPtr = NULL;
    atCmdRegistryPtr->contextPtr = NULL;
}


//--------------------------------------------------------------------------------------------------
/**
 * This function can be used to get the parameters of a received AT command.
 *
 * @return
 *      - LE_OK            The function succeeded.
 *      - LE_FAULT         The function failed to get the requested parameter.
 *
 */
//--------------------------------------------------------------------------------------------------
void le_atServer_GetParameter
(
    le_atServer_ServerCmdRef_t _cmdRef,  ///< [IN] Asynchronous Server Command Reference
    le_atServer_CmdRef_t commandRef, ///< [IN] AT Command Reference
    uint32_t index, ///< [IN] Index of Parameter to be retrieved
    size_t parameterSize  ///< [IN] Size of parameterSize buffer
)
{
    le_result_t result = LE_OK;
    const char* parameter = NULL;

    struct le_atProxy_AtCommandSession* atCmdSessionPtr =
        le_ref_Lookup(atCmdSessionRefMap, commandRef);

    // Verify AT Command Session pointer is valid
    if (atCmdSessionPtr == NULL)
    {
        LE_ERROR("AT Command Session reference pointer is NULL");
        result = LE_FAULT;
    }
    else if (parameterSize < sizeof(atCmdSessionPtr->atCmdParameterList[index]))
    {
        LE_ERROR("Parameter buffer too small");
        result = LE_OVERFLOW;
    }
    else
    {
        // Set pointer to response parameter
        parameter = atCmdSessionPtr->atCmdParameterList[index];
    }

    // Send response to client
    le_atServer_GetParameterRespond(_cmdRef, result, parameter);
}


//--------------------------------------------------------------------------------------------------
/**
 * This function can be used to get the AT command string.
 *
 * @return
 *      - LE_OK            The function succeeded.
 *      - LE_FAULT         The function failed to get the AT command string.
 *
 */
//--------------------------------------------------------------------------------------------------
void le_atServer_GetCommandName
(
    le_atServer_ServerCmdRef_t _cmdRef,  ///< [IN] Asynchronous Server Command Reference
    le_atServer_CmdRef_t commandRef,  ///< [IN] AT Command Reference
    size_t nameSize  ///< [IN] Size of nameSize buffer
)
{
    le_result_t result = LE_OK;
    const char* name = NULL;

    struct le_atProxy_AtCommandSession* atCmdSessionPtr =
        le_ref_Lookup(atCmdSessionRefMap, commandRef);

    // Verify AT Command Session pointer is valid
    if (atCmdSessionPtr == NULL)
    {
        LE_ERROR("AT Command Session reference pointer is NULL");
        result = LE_FAULT;
    }
    else if (nameSize < sizeof(AtCmdRegistry[atCmdSessionPtr->registryIndex].commandStr))
    {
        LE_ERROR("Name buffer too small");
        result = LE_OVERFLOW;
    }
    else
    {
        // Set pointer to response command name
        name = AtCmdRegistry[atCmdSessionPtr->registryIndex].commandStr;
    }

    // Send response to client
    le_atServer_GetCommandNameRespond(_cmdRef, result, name);
}


//--------------------------------------------------------------------------------------------------
/**
 * This function can be used to send an intermediate response.
 *
 * @return
 *      - LE_OK            The function succeeded.
 *      - LE_FAULT         The function failed to send the intermediate response.
 *
 */
//--------------------------------------------------------------------------------------------------
void le_atServer_SendIntermediateResponse
(
    le_atServer_ServerCmdRef_t _cmdRef,  ///< [IN] Asynchronous Server Command Reference
    le_atServer_CmdRef_t commandRef,  ///< [IN] AT Command Reference
    const char* LE_NONNULL responseStr  ///< [IN] Intermediate Response String
)
{
    le_result_t result = LE_OK;

    (void) commandRef;

    // Write the responseStr out to the console port
    atProxySerialUart_write((char*) responseStr, strlen(responseStr));
    atProxySerialUart_write("\r\n", strlen("\r\n"));

    le_atServer_SendIntermediateResponseRespond(_cmdRef, result);
}

//--------------------------------------------------------------------------------------------------
/**
 * This function can be used to send the final result code.
 *
 * @return
 *      - LE_OK            The function succeeded.
 *      - LE_FAULT         The function failed to send the final result code.
 *
 */
//--------------------------------------------------------------------------------------------------
void le_atServer_SendFinalResultCode
(
    le_atServer_ServerCmdRef_t _cmdRef,  ///< [IN] Asynchronous Server Command Reference
    le_atServer_CmdRef_t commandRef,  ///< [IN] AT Command Reference
    le_atServer_FinalRsp_t finalResult,  ///< [IN] Final Response Result
    const char* LE_NONNULL pattern,  ///< [IN] Final Response Pattern String
    uint32_t errorCode  ///< [IN] Final Response Error Code
)
{
    le_result_t result = LE_OK;
    char buffer[LE_ATDEFS_RESPONSE_MAX_BYTES];

    struct le_atProxy_AtCommandSession* atCmdSessionPtr =
        le_ref_Lookup(atCmdSessionRefMap, commandRef);

    // Verify AT Command Session pointer is valid
    if (atCmdSessionPtr == NULL)
    {
        LE_ERROR("AT Command Session reference pointer is NULL");
        result = LE_FAULT;
    }

    switch (finalResult)
    {
        case LE_ATSERVER_OK:
            atProxySerialUart_write(LE_AT_PROXY_OK, strlen(LE_AT_PROXY_OK));
            break;

        case LE_ATSERVER_NO_CARRIER:
            atProxySerialUart_write(LE_AT_PROXY_NO_CARRIER, strlen(LE_AT_PROXY_NO_CARRIER));
            break;

        case LE_ATSERVER_NO_DIALTONE:
            atProxySerialUart_write(LE_AT_PROXY_NO_DIALTONE, strlen(LE_AT_PROXY_NO_DIALTONE));
            break;

        case LE_ATSERVER_BUSY:
            atProxySerialUart_write(LE_AT_PROXY_BUSY, strlen(LE_AT_PROXY_BUSY));
            break;

        default:
            snprintf(buffer, LE_ATDEFS_RESPONSE_MAX_LEN, "%s%lu\r\n", pattern, errorCode);
            atProxySerialUart_write(buffer, strlen(buffer));
            break;
    }

    le_atServer_SendFinalResultCodeRespond(_cmdRef, result);

    // After sending out final response, set current AT session to complete
    atProxyCmdHandler_complete();
}

//--------------------------------------------------------------------------------------------------
/**
 * This function can be used to send the unsolicited response.
 *
 * @return
 *      - LE_OK            The function succeeded.
 *      - LE_FAULT         The function failed to send the unsolicited response.
 *
 */
//--------------------------------------------------------------------------------------------------
void le_atServer_SendUnsolicitedResponse
(
    le_atServer_ServerCmdRef_t _cmdRef,            ///< [IN] Asynchronous Server Command Reference
    const char* LE_NONNULL responseStr,            ///< [IN] Unsolicited Response String
    le_atServer_AvailableDevice_t availableDevice, ///< [IN] device to send the unsolicited response
    le_atServer_DeviceRef_t device                 ///< [IN] device reference where the unsolicited
                                                   ///<      response has to be sent
)
{
    LE_UNUSED(availableDevice);
    LE_UNUSED(device);
    le_result_t result = LE_OK;

    // Queue the response and defer outputting it if current AT session is active (in process)
    if (atProxyCmdHandler_isActive())
    {
        atProxyCmdHandler_StoreUnsolicitedResponse(_cmdRef, responseStr);
        return;
    }

    atProxySerialUart_write((char *)responseStr, strlen(responseStr));
    atProxySerialUart_write("\r\n", strlen("\r\n"));

    le_atServer_SendUnsolicitedResponseRespond(_cmdRef, result);
}

//--------------------------------------------------------------------------------------------------
/**
 * This function creates an AT command and registers it into the AT parser.
 *
 * @return
 *      - Reference to the AT command.
 *      - NULL if an error occurs.
 */
//--------------------------------------------------------------------------------------------------
void le_atServer_Create
(
    le_atServer_ServerCmdRef_t _cmdRef, ///< [IN] Asynchronous Server Command Reference
    const char* namePtr                 ///< [IN] AT command name string
)
{
    int i, num = sizeof(AtCmdRegistry)/sizeof(AtCmdRegistry[0]);
    le_atServer_CmdRef_t cmdRef = NULL;

    for (i=0; i<num; i++)
    {
        if (0 == strncmp(AtCmdRegistry[i].commandStr, namePtr, LE_ATDEFS_COMMAND_MAX_LEN))
        {
            break;
        }
    }

    if (i != num)
    {
        cmdRef = AT_PROXY_CONVERT_IND2REF((uint32_t)i);
    }

    le_atServer_CreateRespond(_cmdRef, cmdRef);
}

//-------------------------------------------------------------------------------------------------
/**
 * Component initialisation once for all component instances.
 */
//-------------------------------------------------------------------------------------------------
COMPONENT_INIT_ONCE
{
    le_atProxy_initOnce();
}

//-------------------------------------------------------------------------------------------------
/**
 * Component initialisation.
 */
//-------------------------------------------------------------------------------------------------
COMPONENT_INIT
{
    LE_INFO("Starting AT Proxy");

    // AT Command Reference pool allocation
    AtCmdRefMap = le_ref_InitStaticMap(AtCmdRefMap, AT_CMD_MAX);

    // Call platform-specific initializer
    le_atProxy_init();
}

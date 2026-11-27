// =================================================================================================
// eModbus: Copyright 2020 by Michael Harwerth, Bert Melis and the contributors to eModbus
//               MIT license - see license.md for details
// =================================================================================================
#include "ModbusClientRTU.h"

#if HAS_FREERTOS || HAS_RP2040_FREERTOS

#undef LOCAL_LOG_LEVEL
// #define LOCAL_LOG_LEVEL LOG_LEVEL_VERBOSE
#include "Logging.h"

namespace dtg_emodbus {

// Constructor takes an optional DE/RE pin and queue size
ModbusClientRTU::ModbusClientRTU(int8_t rtsPin, uint16_t queueLimit) :
  ModbusClient(),
  MR_serial(nullptr),
  MR_lastMicros(micros()),
  MR_interval(2000),
  MR_rtsPin(rtsPin),
  MR_qLimit(queueLimit),
  MR_timeoutValue(DEFAULTTIMEOUT),
  MR_useASCII(false),
  MR_skipLeadingZeroByte(false),
  MR_stopRequested(false),
  MR_forcedStop(false) {
    if (MR_rtsPin >= 0) {
      pinMode(MR_rtsPin, OUTPUT);
      MTRSrts = [this](bool level) {
        digitalWrite(MR_rtsPin, level);
      };
      MTRSrts(LOW);
    } else {
      MTRSrts = RTUutils::RTSauto;
    }
}

// Alternative constructor takes an RTS callback function
ModbusClientRTU::ModbusClientRTU(RTScallback rts, uint16_t queueLimit) :
  ModbusClient(),
  MR_serial(nullptr),
  MR_lastMicros(micros()),
  MR_interval(2000),
  MTRSrts(rts),
  MR_qLimit(queueLimit),
  MR_timeoutValue(DEFAULTTIMEOUT),
  MR_useASCII(false),
  MR_skipLeadingZeroByte(false),
  MR_stopRequested(false),
  MR_forcedStop(false) {
    MR_rtsPin = -1;
    MTRSrts(LOW);
}

// Destructor: clean up queue, task etc.
ModbusClientRTU::~ModbusClientRTU() {
  // Kill worker task and clean up request queue
  end();
}

// begin: start worker task - general version
void ModbusClientRTU::begin(Stream& serial, uint32_t baudRate, int coreID, uint32_t userInterval) {
  MR_serial = &serial;
  doBegin(baudRate, coreID, userInterval);
}

#if defined(ESP32) || defined(ESP8266)
// begin: start worker task - HardwareSerial version
void ModbusClientRTU::begin(HardwareSerial& serial, int coreID, uint32_t userInterval) {
  MR_serial = &serial;
  uint32_t baudRate = serial.baudRate();
  serial.setRxFIFOFull(1);
  doBegin(baudRate, coreID, userInterval);
}
#endif 

void ModbusClientRTU::doBegin(uint32_t baudRate, int coreID, uint32_t userInterval) {
  // Task already running? End it in case
  end();

  // Pull down RTS toggle, if necessary
  MTRSrts(LOW);

  // Set minimum interval time
  MR_interval = RTUutils::calculateInterval(baudRate);

  // If user defined interval is longer, use that
  if (MR_interval < userInterval) {
    MR_interval = userInterval;
  }

  // Create unique task name
  char taskName[18];
  snprintf(taskName, 18, "DtgMb%02XRTU", myInstance);
  MR_stopRequested = false;
  MR_forcedStop = false;
  // Start task to handle the queue
#if HAS_FREERTOS
  xTaskCreatePinnedToCore((TaskFunction_t)&handleConnection, taskName, CLIENT_TASK_STACK, this, 6, &worker, coreID >= 0 ? coreID : tskNO_AFFINITY);
#else
  xTaskCreate((TaskFunction_t)&handleConnection, taskName, CLIENT_TASK_STACK, this, 6, &worker);
  EMODBUS_SET_TASK_AFFINITY(worker, coreID);
#endif
  LOG_D("Client task %d started. Interval=%d\n", (uint32_t)worker, MR_interval);
}

// end: stop worker task
void ModbusClientRTU::end() {
  if (worker) {
    // DTG patch (P1): ask the worker to stop itself instead of deleting it.
    // The worker exits only between transactions, so it never dies while it
    // owns heap allocations or while it is inside a user callback holding a
    // lock. Drop queued work first so the wait covers at most one in-flight
    // transaction.
    {
      LOCK_GUARD(lockGuard, qLock);
      while (!requests.empty()) {
        requests.pop();
      }
    }
    MR_stopRequested = true;
    xTaskNotifyGive(worker);

    // Bound the wait by the longest a single transaction can run, plus slack.
    const uint32_t waitMs = MR_timeoutValue + (MR_interval / 1000) + 250;
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(waitMs);
    while (eTaskGetState(worker) != eDeleted &&
           (int32_t)(deadline - xTaskGetTickCount()) > 0) {
      delay(10);
    }

    MR_forcedStop = false;
    if (eTaskGetState(worker) != eDeleted) {
      // DTG patch (P1): last resort. A forced kill can leak the in-flight
      // frame and can leave a mutex held by the callback permanently locked.
      // Callers must log wasForcedStop(); it must never happen in normal use.
      vTaskDelete(worker);
      MR_forcedStop = true;
      LOG_D("Client task %d force-killed after %ums.\n", (uint32_t)worker, waitMs);
    } else {
      LOG_D("Client task %d stopped.\n", (uint32_t)worker);
    }
    worker = nullptr;
    MR_stopRequested = false;
  }
}

// DTG patch (P5): worker stack high-water mark in words, 0 when not running.
uint32_t ModbusClientRTU::workerStackHighWaterMark() const {
  if (!worker) return 0;
  return (uint32_t)uxTaskGetStackHighWaterMark(worker);
}

// setTimeOut: set/change the default interface timeout
void ModbusClientRTU::setTimeout(uint32_t TOV) {
  MR_timeoutValue = TOV;
  LOG_D("Timeout set to %d\n", TOV);
}

// Toggle protocol to ModbusASCII
void ModbusClientRTU::useModbusASCII(unsigned long timeout) {
  MR_useASCII = true;
  MR_timeoutValue = timeout; // Switch timeout to ASCII's value
  LOG_D("Protocol mode: ASCII\n");
}

// Toggle protocol to ModbusRTU
void ModbusClientRTU::useModbusRTU() {
  MR_useASCII = false;
  LOG_D("Protocol mode: RTU\n");
}

// Inquire protocol mode
bool ModbusClientRTU::isModbusASCII() {
  return MR_useASCII;
}

// Toggle skipping of leading 0x00 byte
void ModbusClientRTU::skipLeading0x00(bool onOff) {
  MR_skipLeadingZeroByte = onOff;
  LOG_D("Skip leading 0x00 mode = %s\n", onOff ? "ON" : "OFF");
}

// Return number of unprocessed requests in queue
uint32_t ModbusClientRTU::pendingRequests() {
  return requests.size();
}

// Remove all pending request from queue
void ModbusClientRTU::clearQueue()
{
  std::queue<RequestEntry> empty;
  LOCK_GUARD(lockGuard, qLock);
  // Empty queue
  std::swap(requests, empty);
}

// Base addRequest taking a preformatted data buffer and length as parameters
Error ModbusClientRTU::addRequestM(ModbusMessage msg, uint32_t token) {
  Error rc = SUCCESS;        // Return value

  LOG_D("request for %02X/%02X\n", msg.getServerID(), msg.getFunctionCode());

  // Add it to the queue, if valid
  if (msg) {
    // Queue add successful?
    if (!addToQueue(token, msg)) {
      // No. Return error after deleting the allocated request.
      rc = REQUEST_QUEUE_FULL;
    }
  }

  LOG_D("RC=%02X\n", rc);
  return rc;
}

// Base syncRequest follows the same pattern
ModbusMessage ModbusClientRTU::syncRequestM(ModbusMessage msg, uint32_t token) {
  ModbusMessage response;

  if (msg) {
    // Queue add successful?
    if (!addToQueue(token, msg, true)) {
      // No. Return error after deleting the allocated request.
      response.setError(msg.getServerID(), msg.getFunctionCode(), REQUEST_QUEUE_FULL);
    } else {
      // Request is queued - wait for the result.
      response = waitSync(msg.getServerID(), msg.getFunctionCode(), token);
    }
  } else {
    response.setError(msg.getServerID(), msg.getFunctionCode(), EMPTY_MESSAGE);
  }
  return response;
}

// addBroadcastMessage: create a fire-and-forget message to all servers on the RTU bus
Error ModbusClientRTU::addBroadcastMessage(const uint8_t *data, uint8_t len) {
  Error rc = SUCCESS;        // Return value

  LOG_D("Broadcast request of length %d\n", len);

  // We do only accept requests with data, 0 byte, data and CRC must fit into 256 bytes.
  if (len && len < 254) {
    // Create a "broadcast token"
    uint32_t token = (millis() & 0xFFFFFF) | 0xBC000000;
    ModbusMessage msg;
    
    // Server ID is 0x00 for broadcast
    msg.add((uint8_t)0x00);
    // Append data
    msg.add(data, len);

    // Queue add successful?
    if (!addToQueue(token, msg)) {
      // No. Return error after deleting the allocated request.
      rc = REQUEST_QUEUE_FULL;
    }
  } else {
    rc =  BROADCAST_ERROR;
  }

  LOG_D("RC=%02X\n", rc);
  return rc;
}


// addToQueue: send freshly created request to queue
bool ModbusClientRTU::addToQueue(uint32_t token, ModbusMessage request, bool syncReq) {
  bool rc = false;
  // Did we get one?
  if (request) {
    RequestEntry re(token, request, syncReq);
    // DTG patch (P2): size check and push under the same lock. The old code
    // read requests.size() unlocked, racing the worker task.
    {
      LOCK_GUARD(lockGuard, qLock);
      if (requests.size() < MR_qLimit) {
        requests.push(re);
        rc = true;
      }
    }
    // DTG patch (P3): wake the worker immediately instead of letting the
    // request wait for the next idle poll.
    if (rc && worker) {
      xTaskNotifyGive(worker);
    }
    {
      LOCK_GUARD(cntLock, countAccessM);
      messageCount++;
    }
  }

  LOG_D("RC=%02X\n", rc);
  return rc;
}

// handleConnection: worker task
// This was created in begin() to handle the queue entries
void ModbusClientRTU::handleConnection(ModbusClientRTU *instance) {
  // initially clean the serial buffer
  while (instance->MR_serial->available()) instance->MR_serial->read();
  // DTG patch (P3): bounded wait so a stop requested during startup is seen
  // promptly instead of always blocking for a full 100 ms.
  for (uint8_t i = 0; i < 10 && !instance->MR_stopRequested; ++i) delay(10);

  // Loop until end() asks us to stop
  while (!instance->MR_stopRequested) {
    // DTG patch (P2): read the queue under its lock. Copying the front entry
    // out lets the bus transaction run without holding the lock.
    bool haveRequest = false;
    RequestEntry request(0, ModbusMessage());
    {
      LOCK_GUARD(lockGuard, instance->qLock);
      if (!instance->requests.empty()) {
        request = instance->requests.front();
        haveRequest = true;
      }
    }
    if (haveRequest) {

      LOG_D("Pulled request from queue\n");

      // Send it via Serial
      RTUutils::send(*(instance->MR_serial), instance->MR_lastMicros, instance->MR_interval, instance->MTRSrts, request.msg, instance->MR_useASCII);

      LOG_D("Request sent.\n");
      // HEXDUMP_V("Data", request.msg.data(), request.msg.size());

      // For a broadcast, we will not wait for a response
      if (request.msg.getServerID() != 0 || ((request.token & 0xFF000000) != 0xBC000000)) {
        // This is a regular request, Get the response - if any
        ModbusMessage response = RTUutils::receive(
          'C',
          *(instance->MR_serial), 
          instance->MR_timeoutValue, 
          instance->MR_lastMicros, 
          instance->MR_interval, 
          instance->MR_useASCII,
          instance->MR_skipLeadingZeroByte);
  
        LOG_D("%s response (%d bytes) received.\n", response.size()>1 ? "Data" : "Error", response.size());
        HEXDUMP_V("Data", response.data(), response.size());
  
        // No error in receive()?
        if (response.size() > 1) {
          // No. Check message contents
          // Does the serverID match the requested?
          if (request.msg.getServerID() != response.getServerID()) {
            // No. Return error response
            response.setError(request.msg.getServerID(), request.msg.getFunctionCode(), SERVER_ID_MISMATCH);
          // ServerID ok, but does the FC match as well?
          } else if (request.msg.getFunctionCode() != (response.getFunctionCode() & 0x7F)) {
            // No. Return error response
            response.setError(request.msg.getServerID(), request.msg.getFunctionCode(), FC_MISMATCH);
          } 
        } else {
          // No, we got an error code from receive()
          // Return it as error response
          response.setError(request.msg.getServerID(), request.msg.getFunctionCode(), static_cast<Error>(response[0]));
        }
  
        LOG_D("Response generated.\n");
        HEXDUMP_V("Response packet", response.data(), response.size());

        // If we got an error, count it
        if (response.getError() != SUCCESS) {
          instance->errorCount++;
        }
  
        // Was it a synchronous request?
        if (request.isSyncRequest) {
          // Yes. Put it into the response map
          {
            LOCK_GUARD(sL, instance->syncRespM);
            instance->syncResponse[request.token] = response;
          }
        // No, an async request. Do we have an onResponse handler?
        } else if (instance->onResponse) {
          // Yes. Call it
          instance->onResponse(response, request.token);
        } else {
          // No, but we may have onData or onError handlers
          // Did we get a normal response?
          if (response.getError()==SUCCESS) {
            // Yes. Do we have an onData handler registered?
            if (instance->onData) {
              // Yes. call it
              instance->onData(response, request.token);
            }
          } else {
            // No, something went wrong. All we have is an error
            // Do we have an onError handler?
            if (instance->onError) {
              // Yes. Forward the error code to it
              instance->onError(response.getError(), request.token);
            }
          }
        }
      }
      // Clean-up time. 
      {
        // Safely lock the queue
        LOCK_GUARD(lockGuard, instance->qLock);
        
        // Remove the front queue entry if the queue is not empty
        if (!instance->requests.empty()) { 
          instance->requests.pop();
        }
      }
    } else {
      // DTG patch (P3): block until a request arrives. The timeout is only a
      // safety net for a missed notification; the idle task stays asleep
      // instead of waking every millisecond.
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
    }
  }

  // DTG patch (P1): drop anything queued while we were finishing and exit
  // ourselves, so end() never has to kill us.
  {
    LOCK_GUARD(lockGuard, instance->qLock);
    while (!instance->requests.empty()) {
      instance->requests.pop();
    }
  }
  vTaskDelete(NULL);
}

}  // namespace dtg_emodbus

#endif  // HAS_FREERTOS

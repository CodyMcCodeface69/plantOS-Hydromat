/**
 * Shelly Air Pump Sequence Controller
 *
 * Controls the air pump with configurable on/off timing sequences for pH correction.
 * This offloads the timing logic to the Shelly device, avoiding rapid HTTP requests
 * from the main PlantOS controller.
 *
 * Usage:
 *
 * Start a sequence with timing pattern (times in seconds):
 * curl 'http://<SHELLY-IP>/script/<SCRIPT-ID>/api?action=sequence&id=0&pattern=30,120,30'
 *   -> 30s ON, 120s OFF, 30s ON (alternating, starts with ON), then OFF
 *
 * With finalstate parameter (ensure switch ends in desired state):
 * curl 'http://<SHELLY-IP>/script/<SCRIPT-ID>/api?action=sequence&id=0&pattern=30,120,30&finalstate=1'
 *   -> 30s ON, 120s OFF, 30s ON, then stays ON
 *
 * Start with explicit on/off:
 * curl 'http://<SHELLY-IP>/script/<SCRIPT-ID>/api?action=sequence&id=2&on=30,30&off=120'
 *   -> 30s ON, 120s OFF, 30s ON on switch 2
 *
 * Stop any running sequence:
 * curl 'http://<SHELLY-IP>/script/<SCRIPT-ID>/api?action=stop&id=0'
 *
 * Get current status:
 * curl 'http://<SHELLY-IP>/script/<SCRIPT-ID>/api?action=status&id=0'
 *
 * Simple on/off control:
 * curl 'http://<SHELLY-IP>/script/<SCRIPT-ID>/api?action=on&id=0'
 * curl 'http://<SHELLY-IP>/script/<SCRIPT-ID>/api?action=off&id=3'
 *
 * Parameters:
 *   id         - Switch ID, 0-3 (default: 0)
 *   finalstate - Final switch state after sequence: 0=off, 1=on (default: 0)
 */

let CONFIG = {
  url_segment: "api",
  action_param: "action",
  default_switch_id: 0,  // Default switch if not specified (0-3 for Strip Gen4)
  actions: {},
  registerActionHandler: function (actionParamValue, handler) {
    this.actions[actionParamValue] = handler;
  },
};

// Sequence state per switch (0-3)
let sequenceStates = {
  0: { running: false, currentStep: 0, steps: [], timerId: null, switchId: 0, finalState: false },
  1: { running: false, currentStep: 0, steps: [], timerId: null, switchId: 1, finalState: false },
  2: { running: false, currentStep: 0, steps: [], timerId: null, switchId: 2, finalState: false },
  3: { running: false, currentStep: 0, steps: [], timerId: null, switchId: 3, finalState: false }
};

// Parse and validate switch ID from params
function getSwitchId(qsParams) {
  if (typeof qsParams.id === "undefined" || qsParams.id === null) {
    return CONFIG.default_switch_id;
  }
  let id = Number(qsParams.id);
  if (id !== 0 && id !== 1 && id !== 2 && id !== 3) {
    return null;  // Invalid
  }
  return id;
}

// Parse time string to milliseconds
// Supports: "30" (seconds), "30s" (seconds), "2m" (minutes), "1h" (hours)
function parseTime(timeStr) {
  if (typeof timeStr === "number") {
    return timeStr * 1000;  // Assume seconds
  }
  if (typeof timeStr !== "string" || timeStr === null) {
    return null;
  }

  let multiplier = 1000;  // Default: seconds
  let numStr = timeStr;

  // Check for unit suffix
  if (timeStr.indexOf("h") > -1 || timeStr.indexOf("H") > -1) {
    multiplier = 3600 * 1000;
    numStr = timeStr.replace("h", "").replace("H", "");
  } else if (timeStr.indexOf("m") > -1 || timeStr.indexOf("M") > -1) {
    multiplier = 60 * 1000;
    numStr = timeStr.replace("m", "").replace("M", "");
  } else if (timeStr.indexOf("s") > -1 || timeStr.indexOf("S") > -1) {
    numStr = timeStr.replace("s", "").replace("S", "");
  }

  let value = Number(numStr);
  if (value !== value) {  // NaN check without isNaN
    return null;
  }
  return value * multiplier;
}

// Set switch state
function setSwitch(switchId, state, callback) {
  Shelly.call(
    "Switch.Set",
    { id: switchId, on: state },
    function (res, err_code, err_msg) {
      if (callback) {
        callback(err_code === 0, err_msg);
      }
    }
  );
}

// Execute step for switch 0
function executeStep0() {
  executeStepForSwitch(0);
}
function executeStep1() {
  executeStepForSwitch(1);
}
function executeStep2() {
  executeStepForSwitch(2);
}
function executeStep3() {
  executeStepForSwitch(3);
}

// Get the executor function for a switch
function getExecutor(switchId) {
  if (switchId === 0) return executeStep0;
  if (switchId === 1) return executeStep1;
  if (switchId === 2) return executeStep2;
  if (switchId === 3) return executeStep3;
  return null;
}

// Execute next step in sequence for a specific switch
function executeStepForSwitch(switchId) {
  let state = sequenceStates[switchId];
  if (!state.running) {
    console.log("Switch " + switchId + ": Not running, stopping");
    return;
  }

  let stepIdx = state.currentStep;
  if (stepIdx >= state.steps.length) {
    // Sequence complete - apply final state
    console.log("Switch " + switchId + ": Sequence complete, final state: " + (state.finalState ? "ON" : "OFF"));
    state.running = false;
    state.timerId = null;
    setSwitch(switchId, state.finalState, null);
    return;
  }

  let step = state.steps[stepIdx];
  let durationMs = step.duration_ms;
  let targetState = step.state;

  console.log("Switch " + switchId + " Step " + stepIdx + ": " + (targetState ? "ON" : "OFF") + " for " + durationMs + "ms");

  setSwitch(switchId, targetState, function (success, err) {
    if (!success) {
      console.log("Switch " + switchId + " error: " + err);
      state.running = false;
      return;
    }

    state.currentStep = stepIdx + 1;

    // Schedule next step
    let executor = getExecutor(switchId);
    if (executor) {
      state.timerId = Timer.set(durationMs, false, executor);
    }
  });
}

// Stop any running sequence on a specific switch
function stopSequence(switchId) {
  let state = sequenceStates[switchId];
  if (state.timerId !== null) {
    Timer.clear(state.timerId);
    state.timerId = null;
  }
  state.running = false;
  state.currentStep = 0;
  state.steps = [];
  setSwitch(switchId, false, null);
}

// Action Handlers

// Lightweight ping endpoint - fast health check with minimal processing
let handlePing = function (qsParams, response) {
  response.code = 200;
  response.body = JSON.stringify({
    status: "ok",
    uptime: Shelly.getComponentStatus("sys").uptime,
    ts: Date.now()
  });
  response.send();
};

let handleSequence = function (qsParams, response) {
  let switchId = getSwitchId(qsParams);
  if (switchId === null) {
    response.code = 400;
    response.body = JSON.stringify({ error: "Invalid switch id. Must be 0-3" });
    response.send();
    return;
  }

  // Stop any existing sequence on this switch
  stopSequence(switchId);

  let steps = [];

  if (typeof qsParams.pattern !== "undefined") {
    // Pattern format: "30,120,30" -> 30s ON, 120s OFF, 30s ON (alternating)
    let times = qsParams.pattern.split(",");
    let isOn = true;
    for (let i = 0; i < times.length; i++) {
      let duration = parseTime(times[i]);
      if (duration === null || duration <= 0) {
        response.code = 400;
        response.body = "Invalid time value at position " + i + ": " + times[i];
        response.send();
        return;
      }
      steps.push({ duration_ms: duration, state: isOn });
      isOn = !isOn;
    }
  } else if (typeof qsParams.on !== "undefined") {
    // Explicit on/off format: on=30,30&off=120 -> 30s ON, 120s OFF, 30s ON
    let onTimes = qsParams.on.split(",");
    let offTimes = typeof qsParams.off !== "undefined" ? qsParams.off.split(",") : [];

    for (let i = 0; i < onTimes.length; i++) {
      let onDuration = parseTime(onTimes[i]);
      if (onDuration === null || onDuration <= 0) {
        response.code = 400;
        response.body = "Invalid ON time at position " + i;
        response.send();
        return;
      }
      steps.push({ duration_ms: onDuration, state: true });

      // Add OFF period if available
      if (i < offTimes.length) {
        let offDuration = parseTime(offTimes[i]);
        if (offDuration === null || offDuration <= 0) {
          response.code = 400;
          response.body = "Invalid OFF time at position " + i;
          response.send();
          return;
        }
        steps.push({ duration_ms: offDuration, state: false });
      }
    }
  } else {
    response.code = 400;
    response.body = "Missing pattern or on/off parameters. Use pattern=30,120,30 or on=30,30&off=120";
    response.send();
    return;
  }

  if (steps.length === 0) {
    response.code = 400;
    response.body = "No valid steps in sequence";
    response.send();
    return;
  }

  // Calculate total duration manually (no Array.reduce in Shelly)
  let totalDuration = 0;
  for (let i = 0; i < steps.length; i++) {
    totalDuration = totalDuration + steps[i].duration_ms;
  }

  // Parse finalstate parameter (0=off, 1=on, default=off)
  let finalState = false;
  if (typeof qsParams.finalstate !== "undefined" && qsParams.finalstate !== null) {
    finalState = (qsParams.finalstate === "1" || qsParams.finalstate === "on");
  }

  // Start sequence
  let state = sequenceStates[switchId];
  state.running = true;
  state.currentStep = 0;
  state.steps = steps;
  state.finalState = finalState;

  console.log("Switch " + switchId + ": Starting sequence with " + steps.length + " steps");

  let executor = getExecutor(switchId);
  if (executor) {
    executor();
  }

  response.code = 200;
  response.body = JSON.stringify({
    status: "started",
    switch_id: switchId,
    steps: steps.length,
    total_duration_ms: totalDuration,
    final_state: finalState ? "on" : "off"
  });
  response.send();
};

let handleStop = function (qsParams, response) {
  let switchId = getSwitchId(qsParams);
  if (switchId === null) {
    response.code = 400;
    response.body = JSON.stringify({ error: "Invalid switch id. Must be 0-3" });
    response.send();
    return;
  }

  let wasRunning = sequenceStates[switchId].running;
  stopSequence(switchId);
  response.code = 200;
  response.body = JSON.stringify({
    status: "stopped",
    switch_id: switchId,
    was_running: wasRunning
  });
  response.send();
};

let handleStatus = function (qsParams, response) {
  let switchId = getSwitchId(qsParams);
  if (switchId === null) {
    response.code = 400;
    response.body = JSON.stringify({ error: "Invalid switch id. Must be 0-3" });
    response.send();
    return;
  }

  let state = sequenceStates[switchId];
  let currentStep = state.steps[state.currentStep] || null;

  // Get actual switch state with enhanced info
  Shelly.call("Switch.GetStatus", { id: switchId }, function (res, err_code, err_msg) {
    let status = {
      switch_id: switchId,
      switch_on: err_code === 0 ? res.output : null,
      sequence: {
        running: state.running,
        current_step: state.currentStep,
        total_steps: state.steps.length,
        current_action: currentStep ? (currentStep.state ? "on" : "off") : null,
        final_state: state.finalState ? "on" : "off"
      },
      device: {
        uptime: Shelly.getComponentStatus("sys").uptime
      }
    };
    response.code = 200;
    response.body = JSON.stringify(status);
    response.send();
  });
};

let handleOn = function (qsParams, response) {
  let switchId = getSwitchId(qsParams);
  if (switchId === null) {
    response.code = 400;
    response.body = JSON.stringify({ error: "Invalid switch id. Must be 0-3" });
    response.send();
    return;
  }

  stopSequence(switchId);  // Stop any running sequence
  setSwitch(switchId, true, function (success, err) {
    if (success) {
      response.code = 200;
      response.body = JSON.stringify({ status: "on", switch_id: switchId });
    } else {
      response.code = 500;
      response.body = JSON.stringify({ error: err });
    }
    response.send();
  });
};

let handleOff = function (qsParams, response) {
  let switchId = getSwitchId(qsParams);
  if (switchId === null) {
    response.code = 400;
    response.body = JSON.stringify({ error: "Invalid switch id. Must be 0-3" });
    response.send();
    return;
  }

  stopSequence(switchId);  // Stop any running sequence
  setSwitch(switchId, false, function (success, err) {
    if (success) {
      response.code = 200;
      response.body = JSON.stringify({ status: "off", switch_id: switchId });
    } else {
      response.code = 500;
      response.body = JSON.stringify({ error: err });
    }
    response.send();
  });
};

// Register action handlers
CONFIG.registerActionHandler("ping", handlePing);
CONFIG.registerActionHandler("sequence", handleSequence);
CONFIG.registerActionHandler("stop", handleStop);
CONFIG.registerActionHandler("status", handleStatus);
CONFIG.registerActionHandler("on", handleOn);
CONFIG.registerActionHandler("off", handleOff);

// Query string parser
function parseQS(qs) {
  let params = {};
  if (qs.length === 0) return params;
  let paramsArray = qs.split("&");
  for (let idx in paramsArray) {
    let kv = paramsArray[idx].split("=");
    params[kv[0]] = kv[1] || null;
  }
  return params;
}

// HTTP request handler
function httpServerHandler(request, response) {
  let params = parseQS(request.query);
  let actionParam = params[CONFIG.action_param];

  console.log("Action: " + actionParam + ", Params: " + JSON.stringify(params));

  if (
    typeof actionParam === "undefined" ||
    typeof CONFIG.actions[actionParam] === "undefined" ||
    CONFIG.actions[actionParam] === null
  ) {
    response.code = 400;
    response.body = JSON.stringify({
      error: "Unknown action",
      available_actions: ["ping", "sequence", "stop", "status", "on", "off"],
      usage: {
        ping: "?action=ping",
        sequence: "?action=sequence&id=0&pattern=30,120,30&finalstate=0",
        stop: "?action=stop&id=0",
        status: "?action=status&id=0",
        on: "?action=on&id=0",
        off: "?action=off&id=0"
      },
      note: "id: 0-3 (default 0), finalstate: 0=off, 1=on (default 0)"
    });
    response.send();
  } else {
    CONFIG.actions[actionParam](params, response);
  }
}

// Register HTTP endpoint
HTTPServer.registerEndpoint(CONFIG.url_segment, httpServerHandler);

console.log("Air Pump Sequence Controller ready");
console.log("Endpoint: /script/<ID>/api");
console.log("Switches: 0-3 (use id parameter)");

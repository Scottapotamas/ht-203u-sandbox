Java.perform(function () {
  var JavaInterface = Java.use("com.hcusbsdk.Interface.JavaInterface");
  var F2HelperClass = Java.use("com.hti.xtherm.hti160203u.helper.F2Helper");
  var EnhClass = Java.use("com.hcusbsdk.Interface.USB_IMAGE_ENHANCEMENT");
  var JnaEnhancement = Java.use("com.hcusbsdk.jna.HCUSBSDKByJNA$USB_IMAGE_ENHANCEMENT");

  var ENH_FIELDS = [
    "byNoiseReduceMode",
    "byBirdWatchingMode",
    "byHighLightMode",
    "byHighLightLevel",
    "dwGeneralLevel",
    "dwFrameNoiseReduceLevel",
    "dwInterFrameNoiseReduceLevel",
    "byPaletteMode",
    "byLSEDetailEnabled",
    "byHookEdgeMode",
    "byHookEdgeLevel",
    "dwLSEDetailLevel",
    "byWideTemperatureMode",
    "byWideTemperatureWork",
    "byIspAgcMode",
    "dwWideTemperatureUpThreshold",
    "dwWideTemperatureDownThreshold"
  ];

  function nowIso() {
    return new Date().toISOString();
  }

  function emit(kind, payload) {
    console.log(JSON.stringify({
      ts: nowIso(),
      kind: kind,
      payload: payload
    }));
  }

  function safeString(value) {
    try {
      if (value === null || value === undefined) {
        return null;
      }
      return String(value);
    } catch (e) {
      return "<toString failed: " + e + ">";
    }
  }

  function safeInt(value, fallback) {
    var out = parseInt(safeString(value), 10);
    if (isNaN(out)) {
      return fallback === undefined ? 0 : fallback;
    }
    return out;
  }

  function getDeclaredField(klass, name) {
    var field = klass.getDeclaredField(name);
    field.setAccessible(true);
    return field;
  }

  function getFieldValue(obj, name) {
    return getDeclaredField(obj.getClass(), name).get(obj);
  }

  function setIntField(obj, name, value) {
    getDeclaredField(obj.getClass(), name).setInt(obj, value);
  }

  function setByteField(obj, name, value) {
    getDeclaredField(obj.getClass(), name).setByte(obj, value);
  }

  function dumpValue(value, depth) {
    if (depth === undefined) {
      depth = 0;
    }
    if (depth > 2) {
      return "<max-depth>";
    }
    if (value === null || value === undefined) {
      return null;
    }

    var t = typeof value;
    if (t === "string" || t === "number" || t === "boolean") {
      return value;
    }

    try {
      if (value.$className === "java.lang.String") {
        return String(value);
      }
      if (
        value.$className === "java.lang.Byte" ||
        value.$className === "java.lang.Short" ||
        value.$className === "java.lang.Integer" ||
        value.$className === "java.lang.Long" ||
        value.$className === "java.lang.Boolean"
      ) {
        return safeString(value);
      }
      if (value.$className && value.$className[0] === "[") {
        return {
          type: value.$className,
          length: value.length
        };
      }
    } catch (_) {}

    try {
      var cls = value.getClass();
      var name = String(cls.getName());
      if (name.indexOf("java.lang.") === 0) {
        return safeString(value);
      }
      var out = { type: name };
      var fields = cls.getDeclaredFields();
      for (var i = 0; i < fields.length; i++) {
        try {
          fields[i].setAccessible(true);
          out[String(fields[i].getName())] = dumpValue(fields[i].get(value), depth + 1);
        } catch (_) {}
      }
      return out;
    } catch (_) {}

    return safeString(value);
  }

  function byteArrayToHex(bytes) {
    var out = [];
    for (var i = 0; i < bytes.length; i++) {
      var v = bytes[i];
      if (v < 0) {
        v += 256;
      }
      out.push(("0" + v.toString(16)).slice(-2));
    }
    return out.join("");
  }

  function toSignedByte(value) {
    var v = safeInt(value, 0);
    if (v > 127) {
      return v - 256;
    }
    if (v < -128) {
      return ((v % 256) + 256) % 256 - 256;
    }
    return v;
  }

  function buildJnaEnhancement(interfaceEnh) {
    var jna = JnaEnhancement.$new();
    jna.byNoiseReduceMode.value = toSignedByte(getFieldValue(interfaceEnh, "byNoiseReduceMode"));
    jna.byBirdWatchingMode.value = toSignedByte(getFieldValue(interfaceEnh, "byBirdWatchingMode"));
    jna.byHighLightMode.value = toSignedByte(getFieldValue(interfaceEnh, "byHighLightMode"));
    jna.byHighLightLevel.value = toSignedByte(getFieldValue(interfaceEnh, "byHighLightLevel"));
    jna.dwGeneralLevel.value = safeInt(getFieldValue(interfaceEnh, "dwGeneralLevel"), 0);
    jna.dwFrameNoiseReduceLevel.value = safeInt(getFieldValue(interfaceEnh, "dwFrameNoiseReduceLevel"), 0);
    jna.dwInterFrameNoiseReduceLevel.value = safeInt(getFieldValue(interfaceEnh, "dwInterFrameNoiseReduceLevel"), 0);
    jna.byPaletteMode.value = toSignedByte(getFieldValue(interfaceEnh, "byPaletteMode"));
    jna.byLSEDetailEnabled.value = toSignedByte(getFieldValue(interfaceEnh, "byLSEDetailEnabled"));
    jna.byHookEdgeMode.value = toSignedByte(getFieldValue(interfaceEnh, "byHookEdgeMode"));
    jna.byHookEdgeLevel.value = toSignedByte(getFieldValue(interfaceEnh, "byHookEdgeLevel"));
    jna.dwLSEDetailLevel.value = safeInt(getFieldValue(interfaceEnh, "dwLSEDetailLevel"), 0);
    jna.byWideTemperatureMode.value = toSignedByte(getFieldValue(interfaceEnh, "byWideTemperatureMode"));
    jna.byWideTemperatureWork.value = toSignedByte(getFieldValue(interfaceEnh, "byWideTemperatureWork"));
    jna.byIspAgcMode.value = toSignedByte(getFieldValue(interfaceEnh, "byIspAgcMode"));
    jna.dwWideTemperatureUpThreshold.value = safeInt(getFieldValue(interfaceEnh, "dwWideTemperatureUpThreshold"), 0);
    jna.dwWideTemperatureDownThreshold.value = safeInt(getFieldValue(interfaceEnh, "dwWideTemperatureDownThreshold"), 0);
    jna.dwSize.value = jna.size();
    jna.write();
    return jna;
  }

  function dumpEnhancementBlob(interfaceEnh) {
    var jna = buildJnaEnhancement(interfaceEnh);
    var size = jna.size();
    var ptr = jna.getPointer();
    var bytes = ptr.getByteArray(0, size);
    return {
      size: size,
      hex: byteArrayToHex(bytes)
    };
  }

  function cloneEnhancement(src) {
    var out = EnhClass.$new();
    for (var i = 0; i < ENH_FIELDS.length; i++) {
      var name = ENH_FIELDS[i];
      if (name.indexOf("dw") === 0) {
        setIntField(out, name, safeInt(getFieldValue(src, name), 0));
      } else {
        setByteField(out, name, toSignedByte(getFieldValue(src, name)));
      }
    }
    return out;
  }

  function enhancementSummary(obj) {
    return {
      value: dumpValue(obj),
      jna_blob: dumpEnhancementBlob(obj)
    };
  }

  function runGetter(ji, loginId, label) {
    var obj = EnhClass.$new();
    var ok = ji.USB_GetImageEnhancement.overload("int", "com.hcusbsdk.Interface.USB_IMAGE_ENHANCEMENT").call(ji, loginId, obj);
    emit("getter_result", {
      label: label,
      ok: ok,
      value: dumpValue(obj),
      jna_blob: dumpEnhancementBlob(obj)
    });
    return obj;
  }

  function chooseFirst(className, callback) {
    var found = null;
    Java.choose(className, {
      onMatch: function (obj) {
        if (found === null) {
          found = obj;
        }
      },
      onComplete: function () {
        callback(found);
      }
    });
  }

  function boolFromByte(value) {
    return safeInt(value, 0) !== 0;
  }

  var detailOverload = F2HelperClass.setDetailEnhancement.overload("int", "boolean");
  detailOverload.implementation = function (level, enabled) {
    emit("enter", {
      class_name: "com.hti.xtherm.hti160203u.helper.F2Helper",
      method: "setDetailEnhancement",
      args: [level, enabled]
    });
    var started = Date.now();
    var result = detailOverload.call(this, level, enabled);
    emit("exit", {
      class_name: "com.hti.xtherm.hti160203u.helper.F2Helper",
      method: "setDetailEnhancement",
      duration_ms: Date.now() - started,
      retval: dumpValue(result)
    });
    return result;
  };

  var noiseOverload = F2HelperClass.setNoiseReducation.overload("int", "boolean");
  noiseOverload.implementation = function (level, enabled) {
    emit("enter", {
      class_name: "com.hti.xtherm.hti160203u.helper.F2Helper",
      method: "setNoiseReducation",
      args: [level, enabled]
    });
    var started = Date.now();
    var result = noiseOverload.call(this, level, enabled);
    emit("exit", {
      class_name: "com.hti.xtherm.hti160203u.helper.F2Helper",
      method: "setNoiseReducation",
      duration_ms: Date.now() - started,
      retval: dumpValue(result)
    });
    return result;
  };

  var setImageEnhancementOverload = JavaInterface.USB_SetImageEnhancement.overload("int", "com.hcusbsdk.Interface.USB_IMAGE_ENHANCEMENT");
  setImageEnhancementOverload.implementation = function (loginId, imageEnhancement) {
    emit("enter", {
      class_name: "com.hcusbsdk.Interface.JavaInterface",
      method: "USB_SetImageEnhancement",
      login_userid: loginId,
      value: dumpValue(imageEnhancement),
      jna_blob: dumpEnhancementBlob(imageEnhancement)
    });
    var started = Date.now();
    var result = setImageEnhancementOverload.call(this, loginId, imageEnhancement);
    emit("exit", {
      class_name: "com.hcusbsdk.Interface.JavaInterface",
      method: "USB_SetImageEnhancement",
      login_userid: loginId,
      duration_ms: Date.now() - started,
      retval: dumpValue(result),
      value: dumpValue(imageEnhancement),
      jna_blob: dumpEnhancementBlob(imageEnhancement)
    });
    return result;
  };

  function directSet(ji, loginId, label, base, mutator) {
    var obj = cloneEnhancement(base);
    mutator(obj);
    emit("direct_plan", {
      label: label,
      value: dumpValue(obj),
      jna_blob: dumpEnhancementBlob(obj)
    });
    var ok = ji.USB_SetImageEnhancement.overload("int", "com.hcusbsdk.Interface.USB_IMAGE_ENHANCEMENT").call(ji, loginId, obj);
    emit("direct_result", {
      label: label,
      ok: ok,
      value: dumpValue(obj),
      jna_blob: dumpEnhancementBlob(obj)
    });
    runGetter(ji, loginId, label + "_after");
  }

  emit("setup", {
    tracer: "frida-thermalcam-enhancement-matrix.js"
  });

  chooseFirst("com.hti.xtherm.hti160203u.helper.F2Helper", function (helper) {
    if (helper === null) {
      emit("error", { stage: "locate_helper", error: "No live F2Helper instance found" });
      return;
    }

    try {
      var loginId = safeInt(getFieldValue(helper, "login_userid"), -1);
      var ji = Java.cast(getFieldValue(helper, "javaInterface"), JavaInterface);
      var preview = Boolean(getFieldValue(helper, "isPreview"));
      var baseHelperEnh = cloneEnhancement(getFieldValue(helper, "mImageEnhancement"));

      emit("runtime_state", {
        login_userid: loginId,
        is_preview: preview,
        helper_image_enhancement: enhancementSummary(baseHelperEnh)
      });

      if (!preview || loginId < 0) {
        emit("error", { stage: "preconditions", error: "Preview inactive or login id invalid" });
        return;
      }

      var baseGetterEnh = runGetter(ji, loginId, "initial");

      detailOverload.call(helper, 60, true);
      runGetter(ji, loginId, "detail_60_on_after");

      detailOverload.call(helper, safeInt(getFieldValue(baseHelperEnh, "dwLSEDetailLevel"), 25), boolFromByte(getFieldValue(baseHelperEnh, "byLSEDetailEnabled")));
      runGetter(ji, loginId, "detail_restore_after");

      noiseOverload.call(helper, 70, true);
      runGetter(ji, loginId, "noise_70_on_after");

      noiseOverload.call(helper, 0, false);
      runGetter(ji, loginId, "noise_off_after");

      noiseOverload.call(helper, safeInt(getFieldValue(baseHelperEnh, "dwFrameNoiseReduceLevel"), 50), safeInt(getFieldValue(baseHelperEnh, "byNoiseReduceMode"), 0) !== 0);
      runGetter(ji, loginId, "noise_restore_after");

      directSet(ji, loginId, "birdwatch_on", baseGetterEnh, function (obj) {
        setByteField(obj, "byBirdWatchingMode", 1);
      });

      directSet(ji, loginId, "restore_from_birdwatch", baseGetterEnh, function (_) {});

      directSet(ji, loginId, "highlight_on", baseGetterEnh, function (obj) {
        setByteField(obj, "byHighLightMode", 1);
        setByteField(obj, "byHighLightLevel", 7);
      });

      directSet(ji, loginId, "restore_from_highlight", baseGetterEnh, function (_) {});

      directSet(ji, loginId, "hookedge_on", baseGetterEnh, function (obj) {
        setByteField(obj, "byHookEdgeMode", 1);
        setByteField(obj, "byHookEdgeLevel", 7);
      });

      directSet(ji, loginId, "restore_from_hookedge", baseGetterEnh, function (_) {});

      directSet(ji, loginId, "general_level_9", baseGetterEnh, function (obj) {
        setIntField(obj, "dwGeneralLevel", 9);
      });

      directSet(ji, loginId, "restore_from_general_level", baseGetterEnh, function (_) {});

      directSet(ji, loginId, "agc_mode_0", baseGetterEnh, function (obj) {
        setByteField(obj, "byIspAgcMode", 0);
      });

      directSet(ji, loginId, "restore_from_agc_mode_0", baseGetterEnh, function (_) {});

      directSet(ji, loginId, "wide_mode_1", baseGetterEnh, function (obj) {
        setByteField(obj, "byWideTemperatureMode", 1);
      });

      directSet(ji, loginId, "restore_from_wide_mode", baseGetterEnh, function (_) {});

      directSet(ji, loginId, "wide_work_1", baseGetterEnh, function (obj) {
        setByteField(obj, "byWideTemperatureWork", 1);
      });

      directSet(ji, loginId, "restore_from_wide_work", baseGetterEnh, function (_) {});

      directSet(ji, loginId, "wide_combo", baseGetterEnh, function (obj) {
        setByteField(obj, "byWideTemperatureMode", 1);
        setByteField(obj, "byWideTemperatureWork", 1);
        setIntField(obj, "dwWideTemperatureUpThreshold", 77);
        setIntField(obj, "dwWideTemperatureDownThreshold", 33);
      });

      directSet(ji, loginId, "restore_from_wide_combo", baseGetterEnh, function (_) {});

      runGetter(ji, loginId, "final");
    } catch (e) {
      emit("error", {
        stage: "run",
        error: safeString(e),
        stack: e && e.stack ? String(e.stack) : null
      });
    }
  });
});

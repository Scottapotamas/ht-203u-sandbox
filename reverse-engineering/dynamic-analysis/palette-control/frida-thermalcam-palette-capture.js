Java.perform(function () {
  var JavaInterface = Java.use("com.hcusbsdk.Interface.JavaInterface");
  var F2HelperClass = Java.use("com.hti.xtherm.hti160203u.helper.F2Helper");
  var PaletteType = Java.use("com.hti.xtherm.hti160203u.helper.Config$PaletteType");
  var JnaEnhancement = Java.use("com.hcusbsdk.jna.HCUSBSDKByJNA$USB_IMAGE_ENHANCEMENT");

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

  function getDeclaredField(klass, name) {
    var field = klass.getDeclaredField(name);
    field.setAccessible(true);
    return field;
  }

  function getFieldValue(obj, name) {
    return getDeclaredField(obj.getClass(), name).get(obj);
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
    var v = parseInt(safeString(value), 10);
    if (isNaN(v)) {
      return 0;
    }
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
    jna.dwGeneralLevel.value = parseInt(safeString(getFieldValue(interfaceEnh, "dwGeneralLevel")), 10);
    jna.dwFrameNoiseReduceLevel.value = parseInt(safeString(getFieldValue(interfaceEnh, "dwFrameNoiseReduceLevel")), 10);
    jna.dwInterFrameNoiseReduceLevel.value = parseInt(safeString(getFieldValue(interfaceEnh, "dwInterFrameNoiseReduceLevel")), 10);
    jna.byPaletteMode.value = toSignedByte(getFieldValue(interfaceEnh, "byPaletteMode"));
    jna.byLSEDetailEnabled.value = toSignedByte(getFieldValue(interfaceEnh, "byLSEDetailEnabled"));
    jna.byHookEdgeMode.value = toSignedByte(getFieldValue(interfaceEnh, "byHookEdgeMode"));
    jna.byHookEdgeLevel.value = toSignedByte(getFieldValue(interfaceEnh, "byHookEdgeLevel"));
    jna.dwLSEDetailLevel.value = parseInt(safeString(getFieldValue(interfaceEnh, "dwLSEDetailLevel")), 10);
    jna.byWideTemperatureMode.value = toSignedByte(getFieldValue(interfaceEnh, "byWideTemperatureMode"));
    jna.byWideTemperatureWork.value = toSignedByte(getFieldValue(interfaceEnh, "byWideTemperatureWork"));
    jna.byIspAgcMode.value = toSignedByte(getFieldValue(interfaceEnh, "byIspAgcMode"));
    jna.dwWideTemperatureUpThreshold.value = parseInt(safeString(getFieldValue(interfaceEnh, "dwWideTemperatureUpThreshold")), 10);
    jna.dwWideTemperatureDownThreshold.value = parseInt(safeString(getFieldValue(interfaceEnh, "dwWideTemperatureDownThreshold")), 10);
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

  function runGetter(ji, loginId) {
    var obj = Java.use("com.hcusbsdk.Interface.USB_IMAGE_ENHANCEMENT").$new();
    var ok = ji.USB_GetImageEnhancement.overload("int", "com.hcusbsdk.Interface.USB_IMAGE_ENHANCEMENT").call(ji, loginId, obj);
    emit("getter_result", {
      method: "USB_GetImageEnhancement",
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

  function paletteObjectByName(name) {
    return PaletteType.valueOf(name);
  }

  function paletteMap() {
    var values = PaletteType.values();
    var out = [];
    for (var i = 0; i < values.length; i++) {
      out.push({
        name: String(values[i].name()),
        palette: values[i].getPalette()
      });
    }
    return out;
  }

  var setPaletteOverload = F2HelperClass.setPalette.overload("com.hti.xtherm.hti160203u.helper.Config$PaletteType");
  setPaletteOverload.implementation = function (palette) {
    emit("enter", {
      class_name: "com.hti.xtherm.hti160203u.helper.F2Helper",
      method: "setPalette",
      args: [safeString(palette), palette ? palette.getPalette() : null]
    });
    var started = Date.now();
    var result = setPaletteOverload.call(this, palette);
    emit("exit", {
      class_name: "com.hti.xtherm.hti160203u.helper.F2Helper",
      method: "setPalette",
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

  emit("setup", {
    tracer: "frida-thermalcam-palette-capture.js",
    palette_map: paletteMap()
  });

  chooseFirst("com.hti.xtherm.hti160203u.helper.F2Helper", function (helper) {
    if (helper === null) {
      emit("error", { stage: "locate_helper", error: "No live F2Helper instance found" });
      return;
    }

    try {
      var loginId = parseInt(String(getFieldValue(helper, "login_userid")), 10);
      var ji = Java.cast(getFieldValue(helper, "javaInterface"), JavaInterface);
      var preview = Boolean(getFieldValue(helper, "isPreview"));
      emit("runtime_state", {
        login_userid: loginId,
        is_preview: preview,
        helper_image_enhancement: dumpValue(getFieldValue(helper, "mImageEnhancement"))
      });

      if (!loginId || loginId <= 0 || !preview) {
        emit("error", {
          stage: "precheck",
          error: "preview/login not ready",
          login_userid: loginId,
          is_preview: preview
        });
        return;
      }

      var before = runGetter(ji, loginId);
      var currentPalette = parseInt(safeString(getFieldValue(before, "byPaletteMode")), 10);
      var targetName = currentPalette === 1 ? "BLACK_HOT" : "WHITE_HOT";
      var restoreName = currentPalette === 1 ? "WHITE_HOT" : "BLACK_HOT";

      emit("palette_plan", {
        current_palette: currentPalette,
        target_name: targetName,
        target_value: paletteObjectByName(targetName).getPalette(),
        restore_name: restoreName,
        restore_value: paletteObjectByName(restoreName).getPalette()
      });

      helper.setPalette(paletteObjectByName(targetName));
      runGetter(ji, loginId);
      helper.setPalette(paletteObjectByName(restoreName));
      runGetter(ji, loginId);
    } catch (e) {
      emit("error", { stage: "main", error: safeString(e) });
    }
  });
});

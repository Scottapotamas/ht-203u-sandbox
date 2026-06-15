Java.perform(function () {
  var Builder = Java.use("android.hardware.usb.UsbDevice$Builder");
  var HashMap = Java.use("java.util.HashMap");
  var File = Java.use("java.io.File");
  var FileInputStream = Java.use("java.io.FileInputStream");
  var InputStreamReader = Java.use("java.io.InputStreamReader");
  var BufferedReader = Java.use("java.io.BufferedReader");
  var ParcelFileDescriptor = Java.use("android.os.ParcelFileDescriptor");
  var UsbDeviceConnection = Java.use("android.hardware.usb.UsbDeviceConnection");
  var ActivityThread = Java.use("android.app.ActivityThread");
  var UsbManager = Java.use("android.hardware.usb.UsbManager");
  var JavaInterface = Java.use("com.hcusbsdk.Interface.JavaInterface");
  var F2HelperClass = Java.use("com.hti.xtherm.hti160203u.helper.F2Helper");
  var ShareHelper = Java.use("com.hti.xtherm.hti160203u.helper.ShareHelper");
  var Thread = Java.use("java.lang.Thread");

  var BasicParamClass = Java.use("com.hcusbsdk.Interface.USB_THERMOMETRY_BASIC_PARAM");
  var ExpertCorrectionClass = Java.use("com.hcusbsdk.Interface.USB_THERMOMETRY_EXPERT_CORRECTION_PARAM");
  var ExpertRegionsClass = Java.use("com.hcusbsdk.Interface.USB_THERMOMETRY_EXPERT_REGIONS");
  var TemperatureCorrectClass = Java.use("com.hcusbsdk.Interface.USB_TEMPERATURE_CORRECT");

  var sequenceStarted = false;
  var sequenceFinished = false;

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

  function readSmallFile(path) {
    var fis = null;
    var isr = null;
    var br = null;
    try {
      fis = FileInputStream.$new(path);
      isr = InputStreamReader.$new(fis);
      br = BufferedReader.$new(isr);
      var line = br.readLine();
      return line ? String(line).trim() : null;
    } catch (_) {
      return null;
    } finally {
      try { if (br) br.close(); } catch (_) {}
      try { if (isr) isr.close(); } catch (_) {}
      try { if (fis) fis.close(); } catch (_) {}
    }
  }

  function scanGuestUsbDevicePath() {
    try {
      var root = File.$new("/sys/bus/usb/devices");
      var entries = root.listFiles();
      if (!entries) {
        return null;
      }
      for (var i = 0; i < entries.length; i++) {
        var entry = entries[i];
        var base = String(entry.getAbsolutePath());
        var vid = readSmallFile(base + "/idVendor");
        var pid = readSmallFile(base + "/idProduct");
        if (vid === "2bdf" && pid === "0102") {
          var bus = readSmallFile(base + "/busnum");
          var dev = readSmallFile(base + "/devnum");
          if (bus && dev) {
            var busStr = ("000" + parseInt(bus, 10)).slice(-3);
            var devStr = ("000" + parseInt(dev, 10)).slice(-3);
            return "/dev/bus/usb/" + busStr + "/" + devStr;
          }
        }
      }
    } catch (e) {
      emit("scan_error", { error: safeString(e) });
    }
    return null;
  }

  function usbDeviceSummary(device) {
    if (!device) {
      return null;
    }
    var out = {};
    try { out.name = safeString(device.getDeviceName()); } catch (_) {}
    try { out.vendor_id = device.getVendorId(); } catch (_) {}
    try { out.product_id = device.getProductId(); } catch (_) {}
    try { out.device_class = device.getDeviceClass(); } catch (_) {}
    try { out.device_subclass = device.getDeviceSubclass(); } catch (_) {}
    try { out.device_protocol = device.getDeviceProtocol(); } catch (_) {}
    try { out.manufacturer = safeString(device.getManufacturerName()); } catch (_) {}
    try { out.product = safeString(device.getProductName()); } catch (_) {}
    try { out.serial = safeString(device.getSerialNumber()); } catch (_) {}
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

  function dumpValue(value, depth) {
    if (depth === undefined) {
      depth = 0;
    }
    if (depth > 3) {
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
      if (value.$className && value.$className[0] === "[") {
        var arr = [];
        var limit = Math.min(value.length, 6);
        for (var i = 0; i < limit; i++) {
          arr.push(dumpValue(value[i], depth + 1));
        }
        return {
          type: value.$className,
          length: value.length,
          sample: arr
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
      for (var j = 0; j < fields.length; j++) {
        try {
          fields[j].setAccessible(true);
          out[String(fields[j].getName())] = dumpValue(fields[j].get(value), depth + 1);
        } catch (_) {}
      }
      return out;
    } catch (_) {}

    return safeString(value);
  }

  function copyFields(dst, src) {
    var fields = src.getClass().getDeclaredFields();
    for (var i = 0; i < fields.length; i++) {
      try {
        fields[i].setAccessible(true);
        fields[i].set(dst, fields[i].get(src));
      } catch (_) {}
    }
  }

  function cloneObject(src, ctor) {
    var dst = ctor.$new();
    copyFields(dst, src);
    return dst;
  }

  function helperSnapshot(helper) {
    try {
      return {
        login_userid: safeInt(getFieldValue(helper, "login_userid"), -1),
        is_preview: Boolean(getFieldValue(helper, "isPreview")),
        cache: dumpValue(getFieldValue(helper, "mThermometryBasicParam")),
        expert_regions: dumpValue(getFieldValue(helper, "mThermometryExpertRegions"))
      };
    } catch (e) {
      return {
        error: safeString(e)
      };
    }
  }

  function chooseBestHelper(callback) {
    var candidates = [];
    var best = null;
    Java.choose("com.hti.xtherm.hti160203u.helper.F2Helper", {
      onMatch: function (obj) {
        var snapshot = helperSnapshot(obj);
        candidates.push(snapshot);
        if (snapshot.is_preview && snapshot.login_userid >= 0) {
          best = obj;
        } else if (best === null) {
          best = obj;
        }
      },
      onComplete: function () {
        emit("helper_inventory", {
          count: candidates.length,
          candidates: candidates
        });
        callback(best);
      }
    });
  }

  function logSharedPrefs(label) {
    emit("share_state", {
      label: label,
      emissivity: ShareHelper.getEmissivity(),
      distance: ShareHelper.getDistance(),
      reflection_enabled: ShareHelper.getRefleationEnable(),
      reflection: ShareHelper.getRefleation()
    });
  }

  function installUsbBypass() {
    var devicePath = scanGuestUsbDevicePath() || "/dev/bus/usb/004/002";
    var ctx = ActivityThread.currentActivityThread().getSystemContext();
    var SerialReader = Java.registerClass({
      name: "com.openai.UsbSerialReaderThermoFollowup",
      implements: [Java.use("android.hardware.usb.IUsbSerialReader")],
      methods: {
        getSerial: [{
          returnType: "java.lang.String",
          argumentTypes: ["java.lang.String"],
          implementation: function () { return "F10615613"; }
        }],
        asBinder: [{
          returnType: "android.os.IBinder",
          argumentTypes: [],
          implementation: function () { return null; }
        }]
      }
    });

    var cfgs = Java.array("android.hardware.usb.UsbConfiguration", []);
    var fakeDev = Builder.$new(
      devicePath,
      0x2bdf,
      0x0102,
      239,
      2,
      1,
      "HIK",
      "HikCamera",
      "4.09",
      cfgs,
      "F10615613",
      true,
      false,
      false,
      false,
      false
    ).build(SerialReader.$new());

    UsbManager.getDeviceList.implementation = function () {
      var map = HashMap.$new();
      map.put("HikCamera", fakeDev);
      emit("usb_override", {
        method: "getDeviceList",
        device_path: devicePath,
        device: usbDeviceSummary(fakeDev)
      });
      return map;
    };

    UsbManager.hasPermission.overloads.forEach(function (overload) {
      overload.implementation = function () {
        emit("usb_override", {
          method: "hasPermission",
          retval: true
        });
        return true;
      };
    });

    UsbManager.requestPermission.overloads.forEach(function (overload) {
      overload.implementation = function () {
        emit("usb_override", {
          method: "requestPermission",
          retval: null
        });
        return;
      };
    });

    UsbManager.openDevice.implementation = function (dev) {
      var path = safeString(dev.getDeviceName());
      try {
        var pfd = ParcelFileDescriptor.open(File.$new(path), ParcelFileDescriptor.MODE_READ_WRITE.value);
        var conn = UsbDeviceConnection.$new(dev);
        var ok = conn.open(path, pfd, ctx);
        emit("usb_override", {
          method: "openDevice",
          device: dumpValue(dev),
          path: path,
          opened: ok,
          fd: ok ? conn.getFileDescriptor() : -1
        });
        return ok ? conn : null;
      } catch (e) {
        emit("usb_override", {
          method: "openDevice",
          path: path,
          error: safeString(e)
        });
        return null;
      }
    };

    emit("setup", {
      usb_bypass: "installed",
      device_path: devicePath
    });
  }

  function getBasicParam(ji, loginId, label) {
    var obj = BasicParamClass.$new();
    var ok = ji.USB_GetThermometryBasicParam.overload("int", "com.hcusbsdk.Interface.USB_THERMOMETRY_BASIC_PARAM").call(ji, loginId, obj);
    emit("getter_basic", {
      label: label,
      ok: ok,
      value: dumpValue(obj)
    });
    return obj;
  }

  function setBasicParam(ji, loginId, obj, label) {
    var ok = ji.USB_SetThermometryBasicParam.overload("int", "com.hcusbsdk.Interface.USB_THERMOMETRY_BASIC_PARAM").call(ji, loginId, obj);
    emit("setter_basic", {
      label: label,
      ok: ok,
      value: dumpValue(obj)
    });
    return ok;
  }

  function getExpertCorrection(ji, loginId, label) {
    var obj = ExpertCorrectionClass.$new();
    var ok = ji.USB_GetExpertCorrectionParam.overload("int", "com.hcusbsdk.Interface.USB_THERMOMETRY_EXPERT_CORRECTION_PARAM").call(ji, loginId, obj);
    emit("getter_expert_correction", {
      label: label,
      ok: ok,
      value: dumpValue(obj)
    });
    return obj;
  }

  function setExpertCorrection(ji, loginId, obj, label) {
    var ok = ji.USB_SetExpertCorrectionParam.overload("int", "com.hcusbsdk.Interface.USB_THERMOMETRY_EXPERT_CORRECTION_PARAM").call(ji, loginId, obj);
    emit("setter_expert_correction", {
      label: label,
      ok: ok,
      value: dumpValue(obj)
    });
    return ok;
  }

  function getExpertRegions(ji, loginId, label) {
    var obj = ExpertRegionsClass.$new();
    var ok = ji.USB_GetThermometryExpertRegions.overload("int", "com.hcusbsdk.Interface.USB_THERMOMETRY_EXPERT_REGIONS").call(ji, loginId, obj);
    emit("getter_expert_regions", {
      label: label,
      ok: ok,
      value: dumpValue(obj)
    });
    return obj;
  }

  function setExpertRegions(ji, loginId, obj, label) {
    var ok = ji.USB_SetThermometryExpertRegions.overload("int", "com.hcusbsdk.Interface.USB_THERMOMETRY_EXPERT_REGIONS").call(ji, loginId, obj);
    emit("setter_expert_regions", {
      label: label,
      ok: ok,
      value: dumpValue(obj)
    });
    return ok;
  }

  function getTemperatureCorrect(ji, loginId, label) {
    var obj = TemperatureCorrectClass.$new();
    var ok = ji.USB_GetTemperatureCorrect.overload("int", "com.hcusbsdk.Interface.USB_TEMPERATURE_CORRECT").call(ji, loginId, obj);
    emit("getter_temperature_correct", {
      label: label,
      ok: ok,
      value: dumpValue(obj)
    });
    return obj;
  }

  function setTemperatureCorrect(ji, loginId, obj, label) {
    var ok = ji.USB_SetTemperatureCorrect.overload("int", "com.hcusbsdk.Interface.USB_TEMPERATURE_CORRECT").call(ji, loginId, obj);
    emit("setter_temperature_correct", {
      label: label,
      ok: ok,
      value: dumpValue(obj)
    });
    return ok;
  }

  function hookBasicSetter() {
    var overload = JavaInterface.USB_SetThermometryBasicParam.overload("int", "com.hcusbsdk.Interface.USB_THERMOMETRY_BASIC_PARAM");
    overload.implementation = function (loginId, param) {
      emit("hook_basic_set_enter", {
        login_userid: loginId,
        value: dumpValue(param)
      });
      var started = Date.now();
      var result = overload.call(this, loginId, param);
      emit("hook_basic_set_exit", {
        login_userid: loginId,
        duration_ms: Date.now() - started,
        retval: result,
        value: dumpValue(param)
      });
      return result;
    };
  }

  function hookUnary(classRef, className, methodName) {
    var overload = classRef[methodName].overload("int");
    overload.implementation = function (loginId) {
      emit("hook_unary_enter", {
        class_name: className,
        method: methodName,
        login_userid: loginId
      });
      var started = Date.now();
      var result = overload.call(this, loginId);
      emit("hook_unary_exit", {
        class_name: className,
        method: methodName,
        login_userid: loginId,
        duration_ms: Date.now() - started,
        retval: result
      });
      return result;
    };
  }

  function hookHelperMutation(methodName, signature) {
    var overload = F2HelperClass[methodName].overload.apply(F2HelperClass[methodName], signature);
    overload.implementation = function () {
      var args = Array.prototype.slice.call(arguments);
      emit("hook_helper_enter", {
        method: methodName,
        args: args.map(function (arg) { return dumpValue(arg); }),
        helper: helperSnapshot(this)
      });
      var started = Date.now();
      var result = overload.apply(this, args);
      emit("hook_helper_exit", {
        method: methodName,
        duration_ms: Date.now() - started,
        retval: dumpValue(result),
        helper: helperSnapshot(this)
      });
      return result;
    };
  }

  function runSequence(reason) {
    if (sequenceStarted || sequenceFinished) {
      return;
    }
    sequenceStarted = true;

    chooseBestHelper(function (helper) {
      if (helper === null) {
        sequenceStarted = false;
        emit("sequence_wait", {
          reason: reason,
          stage: "helper_missing"
        });
        return;
      }

      try {
        var loginId = safeInt(getFieldValue(helper, "login_userid"), -1);
        var preview = Boolean(getFieldValue(helper, "isPreview"));
        var ji = Java.cast(getFieldValue(helper, "javaInterface"), JavaInterface);
        var cache = getFieldValue(helper, "mThermometryBasicParam");
        var expertRegionsCache = getFieldValue(helper, "mThermometryExpertRegions");

        emit("runtime_state", {
          reason: reason,
          login_userid: loginId,
          is_preview: preview,
          cache: dumpValue(cache),
          expert_regions_cache: dumpValue(expertRegionsCache)
        });

        if (!preview || loginId < 0 || cache === null) {
          sequenceStarted = false;
          emit("sequence_wait", {
            reason: reason,
            stage: "preconditions",
            login_userid: loginId,
            is_preview: preview,
            has_cache: cache !== null
          });
          return;
        }

        logSharedPrefs("baseline");
        var baseline = getBasicParam(ji, loginId, "baseline");
        Thread.sleep(700);
        copyFields(cache, baseline);
        emit("cache_hydrated", {
          source: "baseline_getter",
          cache: dumpValue(cache)
        });

        emit("action", { label: "helper_set_emissivity", value: 80 });
        ShareHelper.setEmissivity(80);
        logSharedPrefs("after_share_emissivity_80");
        helper.setEmissivity.overload("int").call(helper, 80);
        Thread.sleep(700);
        getBasicParam(ji, loginId, "after_helper_set_emissivity_80");
        Thread.sleep(500);

        var directEmissivity = cloneObject(baseline, BasicParamClass);
        directEmissivity.dwEmissivity.value = 80;
        emit("action", { label: "direct_set_emissivity", value: 80 });
        setBasicParam(ji, loginId, directEmissivity, "direct_set_emissivity_80");
        Thread.sleep(700);
        getBasicParam(ji, loginId, "after_direct_set_emissivity_80");
        Thread.sleep(500);

        emit("action", { label: "helper_set_distance", value: 120 });
        ShareHelper.setDistance(120);
        logSharedPrefs("after_share_distance_120");
        helper.setDistance.overload("int").call(helper, 120);
        Thread.sleep(700);
        getBasicParam(ji, loginId, "after_helper_set_distance_120");
        Thread.sleep(500);

        var directDistance = cloneObject(baseline, BasicParamClass);
        directDistance.byDistanceUnit.value = 2;
        directDistance.dwDistance.value = 120;
        emit("action", { label: "direct_set_distance", value: 120, unit: 2 });
        setBasicParam(ji, loginId, directDistance, "direct_set_distance_120");
        Thread.sleep(700);
        getBasicParam(ji, loginId, "after_direct_set_distance_120");
        Thread.sleep(700);

        emit("action", { label: "restore_baseline_after_basic" });
        ShareHelper.setEmissivity(baseline.dwEmissivity.value);
        ShareHelper.setDistance(baseline.dwDistance.value);
        copyFields(cache, baseline);
        setBasicParam(ji, loginId, cache, "restore_basic_baseline");
        Thread.sleep(900);
        getBasicParam(ji, loginId, "after_restore_basic");
        Thread.sleep(800);

        var expertBaseline = getExpertCorrection(ji, loginId, "expert_correction_baseline");
        Thread.sleep(500);
        var tempCorrectBaseline = getTemperatureCorrect(ji, loginId, "temperature_correct_baseline");
        Thread.sleep(500);
        var regionsBaseline = getExpertRegions(ji, loginId, "expert_regions_baseline");
        Thread.sleep(700);

        emit("action", { label: "manual_correct_via_helper" });
        helper.shutter.overload().call(helper);
        Thread.sleep(1200);
        getExpertCorrection(ji, loginId, "after_manual_get_expert_correction");
        getTemperatureCorrect(ji, loginId, "after_manual_get_temperature_correct");
        Thread.sleep(700);

        emit("action", { label: "background_correct_via_java" });
        ji.USB_SetImageBackGroundCorrect.overload("int").call(ji, loginId);
        Thread.sleep(1200);
        getExpertCorrection(ji, loginId, "after_background_get_expert_correction");
        getTemperatureCorrect(ji, loginId, "after_background_get_temperature_correct");
        Thread.sleep(700);

        emit("action", { label: "start_expert_correction_via_java" });
        ji.USB_StartExpertCorrection.overload("int").call(ji, loginId);
        Thread.sleep(1200);
        getExpertCorrection(ji, loginId, "after_start_expert_get_expert_correction");
        getTemperatureCorrect(ji, loginId, "after_start_expert_get_temperature_correct");
        getExpertRegions(ji, loginId, "after_start_expert_get_regions");
        Thread.sleep(700);

        emit("action", { label: "direct_set_expert_correction_roundtrip" });
        setExpertCorrection(ji, loginId, cloneObject(expertBaseline, ExpertCorrectionClass), "expert_correction_roundtrip");
        Thread.sleep(700);
        getExpertCorrection(ji, loginId, "after_expert_correction_roundtrip");
        Thread.sleep(700);

        emit("action", { label: "direct_set_temperature_correct_roundtrip" });
        setTemperatureCorrect(ji, loginId, cloneObject(tempCorrectBaseline, TemperatureCorrectClass), "temperature_correct_roundtrip");
        Thread.sleep(700);
        getTemperatureCorrect(ji, loginId, "after_temperature_correct_roundtrip");
        Thread.sleep(700);

        emit("action", { label: "direct_set_expert_regions_roundtrip" });
        setExpertRegions(ji, loginId, cloneObject(regionsBaseline, ExpertRegionsClass), "expert_regions_roundtrip");
        Thread.sleep(700);
        getExpertRegions(ji, loginId, "after_expert_regions_roundtrip");
        Thread.sleep(500);

        sequenceFinished = true;
        emit("sequence_done", {
          reason: reason
        });
      } catch (e) {
        sequenceStarted = false;
        emit("error", {
          stage: "run_sequence",
          reason: reason,
          error: safeString(e),
          stack: e && e.stack ? String(e.stack) : null
        });
      }
    });
  }

  installUsbBypass();
  hookBasicSetter();
  hookUnary(JavaInterface, "com.hcusbsdk.Interface.JavaInterface", "USB_SetImageBackGroundCorrect");
  hookUnary(JavaInterface, "com.hcusbsdk.Interface.JavaInterface", "USB_SetImageManualCorrect");
  hookUnary(JavaInterface, "com.hcusbsdk.Interface.JavaInterface", "USB_StartExpertCorrection");
  hookHelperMutation("setEmissivity", ["int"]);
  hookHelperMutation("setDistance", ["int"]);
  hookHelperMutation("shutter", []);

  emit("setup", {
    tracer: "frida-thermalcam-followup-emissivity-expert.js"
  });

  setTimeout(function () {
    Java.perform(function () {
      runSequence("delayed_attach");
    });
  }, 1000);

  setInterval(function () {
    Java.perform(function () {
      runSequence("poll");
    });
  }, 3000);
});

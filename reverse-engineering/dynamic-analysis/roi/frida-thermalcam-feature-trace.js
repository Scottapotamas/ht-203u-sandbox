Java.perform(function () {
  var ActivityThread = Java.use("android.app.ActivityThread");
  var Builder = Java.use("android.hardware.usb.UsbDevice$Builder");
  var File = Java.use("java.io.File");
  var FileInputStream = Java.use("java.io.FileInputStream");
  var InputStreamReader = Java.use("java.io.InputStreamReader");
  var BufferedReader = Java.use("java.io.BufferedReader");
  var HashMap = Java.use("java.util.HashMap");
  var UsbDeviceConnection = Java.use("android.hardware.usb.UsbDeviceConnection");
  var ParcelFileDescriptor = Java.use("android.os.ParcelFileDescriptor");
  var JavaInterface = Java.use("com.hcusbsdk.Interface.JavaInterface");

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
      emit("usb_scan_error", { error: safeString(e) });
    }
    return null;
  }

  function installUsbBypass() {
    var devicePath = scanGuestUsbDevicePath();
    if (!devicePath) {
      emit("usb_bypass", { status: "no_device_path" });
      return;
    }

    var ctx = ActivityThread.currentActivityThread().getSystemContext();
    var SerialReader = Java.registerClass({
      name: "com.openai.UsbSerialReaderFeatureTrace",
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
    var fakeDevice = Builder.$new(
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

    var app = ActivityThread.currentApplication();
    var usbManager = Java.cast(
      app.getSystemService("usb"),
      Java.use("android.hardware.usb.UsbManager")
    );

    usbManager.getDeviceList.implementation = function () {
      var map = HashMap.$new();
      map.put("thermalcam-hik", fakeDevice);
      emit("usb_bypass", { status: "device_list_override", path: devicePath });
      return map;
    };

    usbManager.hasPermission.overload("android.hardware.usb.UsbDevice").implementation = function () {
      return true;
    };

    usbManager.requestPermission.overload(
      "android.hardware.usb.UsbDevice",
      "android.app.PendingIntent"
    ).implementation = function (device, pendingIntent) {
      emit("usb_bypass", {
        status: "request_permission_bypassed",
        device_name: safeString(device ? device.getDeviceName() : null)
      });
    };

    usbManager.openDevice.overload("android.hardware.usb.UsbDevice").implementation = function () {
      var pfd = ParcelFileDescriptor.open(File.$new(devicePath), ParcelFileDescriptor.MODE_READ_WRITE.value);
      var conn = UsbDeviceConnection.$new(fakeDevice);
      if (!conn.open(devicePath, pfd, ctx)) {
        emit("usb_bypass", { status: "open_failed", path: devicePath });
        return null;
      }
      emit("usb_bypass", { status: "open_ok", path: devicePath });
      return conn;
    };
  }

  function listToArray(list) {
    var out = [];
    if (!list) {
      return out;
    }
    try {
      var size = list.size();
      for (var i = 0; i < size; i++) {
        out.push(list.get(i));
      }
    } catch (e) {
      emit("list_error", { error: safeString(e) });
    }
    return out;
  }

  function serializePoint(point) {
    if (!point) {
      return null;
    }
    function readField(name, reader) {
      try {
        var field = point.getClass().getDeclaredField(name);
        field.setAccessible(true);
        return reader(field, point);
      } catch (_) {
        return null;
      }
    }
    var obj = {};
    obj.type = readField("type", function (field, instance) {
      var value = field.get(instance);
      return value ? safeString(value.name()) : null;
    });
    obj.x = readField("x", function (field, instance) { return field.getFloat(instance); });
    obj.y = readField("y", function (field, instance) { return field.getFloat(instance); });
    obj.temp_c = readField("temp", function (field, instance) { return field.getFloat(instance); });
    obj.illegal = readField("illegal", function (field, instance) { return !!field.getBoolean(instance); });
    obj.unit = readField("tempunit", function (field, instance) {
      var value = field.get(instance);
      return value ? safeString(value.name()) : null;
    });
    return obj;
  }

  function installMethodHook(className, methodName, wrapper) {
    var klass = Java.use(className);
    klass[methodName].overloads.forEach(function (overload) {
      overload.implementation = wrapper(overload);
    });
  }

  installUsbBypass();

  [
    "USB_SetThermometryMode",
    "USB_GetThermometryMode",
    "USB_SetThermometryBasicParam",
    "USB_GetThermometryBasicParam",
    "USB_StartStreamCallback",
    "USB_GetROITemperatureSearch"
  ].forEach(function (name) {
    try {
      installMethodHook("com.hcusbsdk.Interface.JavaInterface", name, function (original) {
        return function () {
          var args = Array.prototype.slice.call(arguments);
          emit("java_usb_enter", {
            method: name,
            argc: args.length,
            args: args.map(function (arg) { return safeString(arg); })
          });
          var rv = original.apply(this, args);
          emit("java_usb_exit", {
            method: name,
            retval: rv
          });
          return rv;
        };
      });
    } catch (e) {
      emit("hook_error", { class_name: "com.hcusbsdk.Interface.JavaInterface", method: name, error: safeString(e) });
    }
  });

  try {
    installMethodHook("com.hti.xtherm.hti160203u.ui.activity.HomeActivity", "on_f2_temperature", function (original) {
      return function (buf, width, height, flag) {
        var length = null;
        try { length = buf ? buf.length : null; } catch (_) {}
        emit("thermal_frame", {
          width: width,
          height: height,
          bytes: length,
          first_frame_flag: !!flag
        });
        return original.call(this, buf, width, height, flag);
      };
    });
  } catch (e) {
    emit("hook_error", { class_name: "HomeActivity", method: "on_f2_temperature", error: safeString(e) });
  }

  try {
    installMethodHook("com.hti.xtherm.hti160203u.ui.activity.HomeActivity", "onSearchFeature", function (original) {
      return function (list) {
        var points = listToArray(list).map(serializePoint);
        emit("search_feature", {
          count: points.length,
          points: points.slice(0, 8)
        });
        return original.call(this, list);
      };
    });
  } catch (e) {
    emit("hook_error", { class_name: "HomeActivity", method: "onSearchFeature", error: safeString(e) });
  }

  try {
    installMethodHook("com.hti.xtherm.hti160203u.ui.activity.HomeActivity", "onTemperatureAlarm", function (original) {
      return function (alarmType, enabled) {
        emit("temperature_alarm", {
          alarm_type: safeString(alarmType),
          enabled: !!enabled
        });
        return original.call(this, alarmType, enabled);
      };
    });
  } catch (e) {
    emit("hook_error", { class_name: "HomeActivity", method: "onTemperatureAlarm", error: safeString(e) });
  }

  try {
    installMethodHook("com.hti.xtherm.hti160203u.helper.F2Helper", "shutter", function (original) {
      return function () {
        emit("app_action", { method: "F2Helper.shutter" });
        return original.call(this);
      };
    });
  } catch (e) {
    emit("hook_error", { class_name: "F2Helper", method: "shutter", error: safeString(e) });
  }

  emit("ready", { script: "frida-thermalcam-feature-trace.js" });
});

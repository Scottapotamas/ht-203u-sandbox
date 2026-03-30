Java.perform(function () {
  var JavaInterface = Java.use("com.hcusbsdk.Interface.JavaInterface");
  var CalibrationFileClass = Java.use("com.hcusbsdk.Interface.USB_THERMOMETRY_CALIBRATION_FILE");
  var FileOutputStream = Java.use("java.io.FileOutputStream");

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

  emit("setup", {
    tracer: "frida-thermalcam-dump-calibration-file.js",
    mode: "attach-running-process"
  });

  chooseFirst("com.hti.xtherm.hti160203u.helper.F2Helper", function (helper) {
    if (helper === null) {
      emit("error", { message: "No live F2Helper instance found" });
      return;
    }

    try {
      var loginId = parseInt(String(getFieldValue(helper, "login_userid")), 10);
      var ji = Java.cast(getFieldValue(helper, "javaInterface"), JavaInterface);
      var obj = CalibrationFileClass.$new();
      var outPath = "/data/data/com.hti.xtherm.hti160203u/cache/thermometry_calibration_2026-03-28.bin";

      emit("checkpoint", { step: "pre_getter", login_userid: loginId, out_path: outPath });

      var ok = ji.USB_GetThermometryCalibrationFile.overload(
        "int",
        "com.hcusbsdk.Interface.USB_THERMOMETRY_CALIBRATION_FILE"
      ).call(ji, loginId, obj);

      var length = parseInt(String(getFieldValue(obj, "dwFileLenth")), 10);
      var bytes = getFieldValue(obj, "pCalibrationFile");

      emit("checkpoint", { step: "post_getter", ok: ok, length: length });

      if (!ok) {
        emit("result", {
          ok: false,
          login_userid: loginId,
          length: length,
          out_path: outPath
        });
        return;
      }

      var fos = FileOutputStream.$new(outPath);
      fos.write.overload("[B", "int", "int").call(fos, bytes, 0, length);
      fos.flush();
      fos.close();

      emit("result", {
        ok: true,
        login_userid: loginId,
        length: length,
        out_path: outPath
      });
    } catch (e) {
      emit("error", {
        message: "Calibration dump failed",
        error: safeString(e)
      });
    }
  });
});

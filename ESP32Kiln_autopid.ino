#if defined(PID_QUICKPID) || defined(PID_PID_V1)
#include <sTune.h>

#define DEFAULT_TEMP_RISE_AFTER_OFF 30.0
#define CONTROL_HYSTERISIS .01

int measureInterval = 500;
int tuner_id = 5;
double tuner_noise_band = 1;
double tuner_output_step = 1;

float _calP = .5 / DEFAULT_TEMP_RISE_AFTER_OFF;
float _calD = 5.0 / DEFAULT_TEMP_RISE_AFTER_OFF;
float _calI = 4 / DEFAULT_TEMP_RISE_AFTER_OFF;

uint32_t settleTimeSec = 10;
uint32_t testTimeSec = 500;  // runPid interval = testTimeSec / samples
const uint16_t samples = 500;
const float inputSpan = 200;
float outputSpan = Prefs[PRF_PID_WINDOW].value.uint16;
float outputStart = 0;
float outputStep = 50;
float tempLimit = 300;
uint8_t debounce = 0;
bool startup = true;

float Input, Output, Setpoint = 80, Kp, Ki, Kd;  // sTune

sTune tuner = sTune(&Input, &Output, tuner.ZN_PID, tuner.directIP, tuner.printOFF);

#ifdef PID_QUICKPID
QuickPID *myPid;
#elif defined(PID_PID_V1)
double input, output, setpoint = 80, kp, ki, kd;  // PID_v1
PID *myPid;
#endif

void CalibrateInit() {
  outputSpan = Prefs[PRF_PID_WINDOW].value.uint16;
  Program_run_state = PR_CALIBRATE;
  Output = 0;

#ifdef PID_QUICKPID
  myPid = new QuickPID(&Input, &Output, &Setpoint, 0, 0, 0,
                       QuickPID.pMode::pOnError,
                       QuickPID.dMode::dOnError,
                       QuickPID.iAwMode::iAwClamp,
                       QuickPID.Action::direct);
#elif defined(PID_PID_V1)
  myPid = new PID(&input, &output, &setpoint, kp, ki, kd, P_ON_M, DIRECT);
#endif



#ifdef EMR_RELAY_PIN
  Enable_EMR();
#endif

  tuner.Configure(inputSpan, outputSpan, outputStart, outputStep, testTimeSec, settleTimeSec, samples);
  tuner.SetEmergencyStop(tempLimit);

  tuner.SetTuningMethod(tuner.ZN_PID);

  myPid->SetOutputLimits(MIN_WINDOW, outputSpan * 0.1);
#ifdef PID_QUICKPID
  myPid->SetProportionalMode(QuickPID.pMode::pOnError);
  myPid->SetDerivativeMode(QuickPID.dMode::dOnError);
  myPid->SetAntiWindupMode(QuickPID.iAwMode::iAwClamp);
  myPid->SetMode(QuickPID.Control::automatic);
  myPid->SetSampleTimeUs((outputSpan - 1) * 1000);
#elif defined(PID_PID_V1)
  myPid->SetSampleTime(outputSpan - 1);
  myPid->SetMode(AUTOMATIC);
#endif

  Setpoint = Prefs[PRF_PID_AUTO_SETPOINT].value.vfloat;
}

void HandleCalibration() {
  float optimumOutput = tuner.softPwm(SSR1_RELAY_PIN, Input, Output, Setpoint, outputSpan, debounce);

  switch (tuner.Run()) {
    case tuner.sample:
      Update_TemperatureA();
      Input = kiln_temp;
      break;

    case tuner.tunings:                     // active just once when sTune is done
      tuner.GetAutoTunings(&Kp, &Ki, &Kd);  // sketch variables updated by sTune

#ifdef PID_QUICKPID
      myPid->SetOutputLimits(0, outputSpan * 0.1);
      myPid->SetSampleTimeUs((outputSpan - 1) * 1000);
#elif defined(PID_PID_V1)
      myPid->SetOutputLimits(0, outputSpan * 0.1);
      myPid->SetSampleTime((outputSpan - 1));
      setpoint = Setpoint, output = outputStep, kp = Kp, ki = Ki, kd = Kd;
#endif

      debounce = 0;  // ssr mode
      Output = outputStep;
      DBG dbgLog(LOG_INFO, "[AutoPID] Tuning complete Kp: %.2f \t Ki %.2f \t Kd = %.2f\n", Kp, Ki, Kd);
      Program_run_state = PR_ENDED;
#ifdef PID_QUICKPID
      myPid->SetTunings(Kp, Ki, Kd);  // update PID with the new tunings
      myPid->SetMode(QuickPID.Control::manual);
#elif defined(PID_PID_V1)
      myPid->SetTunings(kp, ki, kd);  // update PID with the new tunings
      myPid->SetMode(MANUAL);
#endif
      SSR_Off();

      Change_prefs_value(PrefsName[PRF_PID_KP], String(Kp));
      Change_prefs_value(PrefsName[PRF_PID_KI], String(Ki));
      Change_prefs_value(PrefsName[PRF_PID_KD], String(Kd));

      Save_prefs();
      free(myPid);
      break;

    case tuner.runPid:  // active once per sample after tunings
#ifdef PID_QUICKPID
      if (startup && Input > Setpoint - 5) {
        startup = false;
        Output -= 9;
        myPid->SetMode(QuickPID.Control::manual);
        myPid->SetMode(QuickPID.Control::automatic);
      }
      Update_TemperatureA();
      Input = kiln_temp;
#elif defined(PID_PID_V1)
      input = Input;
#endif
      myPid->Compute();
#if defined(PID_PID_V1)
      Output = output;
#endif
      break;
  }
}
#endif

#ifdef PID_AUTOTUNEPID
void CalibrateInit() {
  Program_run_state = PR_CALIBRATE;
  KilnPID.setManualGains(Prefs[PRF_PID_KP].value.vfloat,
                          Prefs[PRF_PID_KI].value.vfloat,
                          Prefs[PRF_PID_KD].value.vfloat);
  KilnPID.setSetpoint(Prefs[PRF_PID_AUTO_SETPOINT].value.vfloat);
  KilnPID.setTuningMethod(TuningMethod::ZieglerNichols);
  KilnPID.setOperationalMode(OperationalMode::Tune);
  KilnPID.setOscillationMode(OscillationMode::Half);
}

void HandleCalibration() {
  if (KilnPID.getOperationalMode() == OperationalMode::Normal) {
    float Kp = KilnPID.getKp();
    float Ki = KilnPID.getKi();
    float Kd = KilnPID.getKd();

    DBG dbgLog(LOG_INFO, "[AutoPID] Tuning complete Kp: %.2f \t Ki %.2f \t Kd = %.2f\n", Kp, Ki, Kd);
    Program_run_state = PR_ENDED;
    SSR_Off();

    Change_prefs_value(PrefsName[PRF_PID_KP], String(Kp));
    Change_prefs_value(PrefsName[PRF_PID_KI], String(Ki));
    Change_prefs_value(PrefsName[PRF_PID_KD], String(Kd));

    Save_prefs();
  }
}
#endif

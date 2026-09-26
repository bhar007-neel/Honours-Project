--  Acceptance gate for sensor readings received by the QNX supervisor.
--
--  Types mirror the wire format in shared/protocol/edge_protocol.h, so every
--  value the C decoder can produce is representable here. The limits are the
--  BME280 operating range. GNATprove proves that:
--    * no run-time error (overflow, range violation) can occur;
--    * a reading is accepted exactly when every field is within its limits;
--    * the rejection reason always names the first field that is out of range;
--    * the step check cannot overflow even for extreme temperature pairs.

package Sensor_Gate with SPARK_Mode is

   type Centi_Celsius is range -32_768 .. 32_767;         --  int16_t on the wire
   type Centi_Percent is range 0 .. 65_535;               --  uint16_t
   type Pascal        is range 0 .. 4_294_967_295;        --  uint32_t

   Min_Temperature : constant Centi_Celsius := -4_000;    --  -40.00 degC
   Max_Temperature : constant Centi_Celsius :=  8_500;    --   85.00 degC
   Max_Humidity    : constant Centi_Percent := 10_000;    --  100.00 %RH
   Min_Pressure    : constant Pascal        := 30_000;    --  300 hPa
   Max_Pressure    : constant Pascal        := 110_000;   --  1100 hPa

   type Reading is record
      Temperature : Centi_Celsius;
      Humidity    : Centi_Percent;
      Pressure    : Pascal;
   end record;

   type Verdict is
     (Accepted,
      Temperature_Out_Of_Range,
      Humidity_Out_Of_Range,
      Pressure_Out_Of_Range);

   function Temperature_Ok (R : Reading) return Boolean is
     (R.Temperature in Min_Temperature .. Max_Temperature);

   function Humidity_Ok (R : Reading) return Boolean is
     (R.Humidity <= Max_Humidity);

   function Pressure_Ok (R : Reading) return Boolean is
     (R.Pressure in Min_Pressure .. Max_Pressure);

   function In_Range (R : Reading) return Boolean is
     (Temperature_Ok (R) and then Humidity_Ok (R) and then Pressure_Ok (R));

   function Check (R : Reading) return Verdict with
     Post => (Check'Result = Accepted) = In_Range (R),
     Contract_Cases =>
       (not Temperature_Ok (R) =>
          Check'Result = Temperature_Out_Of_Range,
        Temperature_Ok (R) and then not Humidity_Ok (R) =>
          Check'Result = Humidity_Out_Of_Range,
        Temperature_Ok (R) and then Humidity_Ok (R) and then not Pressure_Ok (R) =>
          Check'Result = Pressure_Out_Of_Range,
        In_Range (R) =>
          Check'Result = Accepted);

   --  Rejects physically implausible jumps between consecutive samples.
   subtype Step_Limit is Centi_Celsius range 0 .. Centi_Celsius'Last;

   function Temperature_Step_Ok
     (Previous, Current : Centi_Celsius;
      Max_Step          : Step_Limit) return Boolean
   with
     Post => Temperature_Step_Ok'Result =
               (abs (Long_Integer (Current) - Long_Integer (Previous))
                  <= Long_Integer (Max_Step));

end Sensor_Gate;

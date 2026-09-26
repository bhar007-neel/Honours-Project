package body Sensor_Gate with SPARK_Mode is

   function Check (R : Reading) return Verdict is
   begin
      if not Temperature_Ok (R) then
         return Temperature_Out_Of_Range;
      elsif not Humidity_Ok (R) then
         return Humidity_Out_Of_Range;
      elsif not Pressure_Ok (R) then
         return Pressure_Out_Of_Range;
      else
         return Accepted;
      end if;
   end Check;

   function Temperature_Step_Ok
     (Previous, Current : Centi_Celsius;
      Max_Step          : Step_Limit) return Boolean
   is
      --  Current - Previous can reach +/-65_535, outside Centi_Celsius, so the
      --  difference is computed in a wider type. GNATprove flags the overflow
      --  if this conversion is removed.
      type Wide is range -70_000 .. 70_000;
      Difference : constant Wide := Wide (Current) - Wide (Previous);
   begin
      return abs Difference <= Wide (Max_Step);
   end Temperature_Step_Ok;

end Sensor_Gate;

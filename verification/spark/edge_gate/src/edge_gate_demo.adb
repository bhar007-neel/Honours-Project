with Ada.Text_IO; use Ada.Text_IO;
with Sensor_Gate;  use Sensor_Gate;

procedure Edge_Gate_Demo is

   procedure Show (Label : String; R : Reading) is
   begin
      Put_Line (Label & " -> " & Verdict'Image (Check (R)));
   end Show;

begin
   Show ("normal room reading   ", (Temperature => 2_345, Humidity => 4_510, Pressure => 101_325));
   Show ("sensor fault, 120 degC", (Temperature => 12_000, Humidity => 4_510, Pressure => 101_325));
   Show ("humidity above 100 %  ", (Temperature => 2_345, Humidity => 10_001, Pressure => 101_325));
   Show ("pressure too low      ", (Temperature => 2_345, Humidity => 4_510, Pressure => 29_999));
   Show ("boundary values       ", (Temperature => -4_000, Humidity => 10_000, Pressure => 110_000));

   Put_Line ("step 22.00 -> 22.40 degC (max 1.00): "
             & Boolean'Image (Temperature_Step_Ok (2_200, 2_240, 100)));
   Put_Line ("step -327.68 -> 327.67 degC (max 1.00): "
             & Boolean'Image (Temperature_Step_Ok (Centi_Celsius'First, Centi_Celsius'Last, 100)));
end Edge_Gate_Demo;

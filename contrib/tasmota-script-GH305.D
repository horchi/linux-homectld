>D
>B
=>sensor53 r
>M 1
+1,3,s,0,9600,data,1
1,77070100010800ff@1000,Total Verbrauch,kWh,Total_in,3
1,77070100020800ff@1000,Total Einspeisung,kWh,Total_out,3
1,=h <br/>
1,77070100100700ff@1,Actual Load,W,Power_curr,0
1,=h <br/>
1,=m 9+10+11 @1,Current,A,Curr_summ,3
1,=m 12+13+14/#3 @1,Voltage Avg,V,Volt_avg,3
1,=h <br/>
1,77070100240700ff@1,Consumption L1,W,Power_p1,2
1,77070100380700ff@1,Consumption L2,W,Power_p2,2
1,770701004c0700ff@1,Consumption L3,W,Power_p3,2
1,=h <br/>
1,770701001f0700ff@1,Current L1,A,Curr_p1,3
1,77070100330700ff@1,Current L2,A,Curr_p2,3
1,77070100470700ff@1,Current L3,A,Curr_p3,3
1,=h <br/>
1,77070100200700ff@1,Voltage L1,V,Volt_p1,3
1,77070100340700ff@1,Voltage L2,V,Volt_p2,3
1,77070100480700ff@1,Voltage L3,V,Volt_p3,3
1,=h <br/>
1,77070100510701ff@1,Phaseangle L2-L1,°,phase_angle_L2_L1,0
1,77070100510702ff@1,Phaseangle L3-L1,°,phase_angle_L3_L1,0
1,77070100510704ff@1,Phaseangle I/U L1,°,phase_angle_L1,1
1,7707010051070fff@1,Phaseangle I/U L2,°,phase_angle_L2,1
1,7707010051071aff@1,Phaseangle I/U L3,°,phase_angle_L3,1
1,770701000e0700ff@1,Frequency,Hz,Freq,1
;1,=h=======UNBEKANNT===========
;1,77070100000009FF@#,unbekannt1,,Power_Use_Sum,3
;1,77070100000000FF@#,unbekannt2,,Power_Use_Sum,3
;1,7707010060320101@#,unbekannt3,,Meter_id,0
;1,77010b0a01445a47@#,unbekannt4,,Meter_id,0
;1,77070100600100ff@#,unbekannt5,,Meter_id,0
1,=h
#
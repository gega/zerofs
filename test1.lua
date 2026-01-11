local m = require('fstest');

local chunk = 2000000;

m.speed(0,250);
m.setdir("data");
m.setstep(false);
m.badblock(false)

m.setmode("write");
st=m.write("frog.qla", chunk);
if (st~=0) then m.assert("write frog.qla"); end

st=m.write("homework.qla", chunk);
if (st~=0) then m.assert("write homework.qla"); end
st=m.write("zerofs.qli", chunk);
if (st~=0) then m.assert("write zerofs.qli"); end
st=m.write("swim.qla", chunk);
if (st~=0) then m.assert("write swim.qla"); end

m.setmode("read");
m.printdebug();

st=m.verify("zerofs.qli");
if (st~=0) then m.assert("verify zerofs.qli"); end
m.verify("frog.qla");
if (st~=0) then m.assert("verify frog.qla"); end
m.verify("swim.qla");
if (st~=0) then m.assert("verify swim.qla"); end
m.verify("homework.qla");
if (st~=0) then m.assert("verify homework.qla"); end


m.setmode("write");
m.delete("homework.qla");
m.delete("frog.qla");

m.write("bench.qla", chunk);
if (st~=0) then m.assert("write bench.qla"); end
m.write("citynite.qla", chunk);
if (st~=0) then m.assert("write citynite.qla"); end
m.write("driving.qla", chunk);
if (st~=0) then m.assert("write driving.qla"); end
m.write("metro.qla", chunk);
if (st~=0) then m.assert("write metro.qla"); end
m.write("play.qla", chunk);
if (st~=0) then m.assert("write play.qla"); end
m.write("setal.qla", chunk);
if (st~=0) then m.assert("write setal.qla"); end


m.setmode("read");
m.verify("swim.qla");
if (st~=0) then m.assert("verify swim.qla"); end

m.verify("zerofs.qli");
if (st~=0) then m.assert("verify zerofs.qli"); end

m.verify("bench.qla");
if (st~=0) then m.assert("verify bench.qla"); end
m.verify("citynite.qla");
if (st~=0) then m.assert("verify citynite.qla"); end
m.verify("driving.qla");
if (st~=0) then m.assert("verify driving.qla"); end
m.verify("metro.qla");
if (st~=0) then m.assert("verify metro.qla"); end
m.verify("play.qla");
if (st~=0) then m.assert("verify play.qla"); end
m.verify("setal.qla");
if (st~=0) then m.assert("verify setal.qla"); end


m.setmode("write");
m.delete("driving.qla");
m.write("falcon.qla", chunk);
if (st~=0) then m.assert("write falcon.qla"); end


m.setmode("read");

--m.setstep(false);
--m.speed(0);

m.verify("swim.qla");
if (st~=0) then m.assert("verify swim.qla"); end
m.verify("zerofs.qli");
if (st~=0) then m.assert("verify zerofs.qli"); end
m.verify("falcon.qla");
if (st~=0) then m.assert("verify falcon.qla"); end
m.verify("metro.qla");
if (st~=0) then m.assert("verify metro.qla"); end
m.verify("citynite.qla");
if (st~=0) then m.assert("verify citynite.qla"); end

m.setmode("write");
st=m.delete("metro.qla");
if(st==0) then warn("delete OK"); end
if (st~=0) then m.assert("delete metro.qla"); end
m.delete("play.qla");
if (st~=0) then m.assert("delete play.qla"); end

m.write("eating.qla", chunk);
if (st~=0) then m.assert("write eating.qla"); end
m.write("frog.qla", chunk);
if (st~=0) then m.assert("write frog.qla"); end

m.setmode("read");
m.verify("eating.qla");
if (st~=0) then m.assert("verify eating.qla"); end
m.verify("frog.qla");
if (st~=0) then m.assert("verify frog.qla"); end

m.setmode("write");
st=m.write("metro.qla", chunk);
if(st~=0) then m.assert("write metro.qla"); end
warn("DISK FULL VERIFIED");

--m.setstep(true);
--m.getch();
st=m.write("driving.qla", chunk);
if(st==0) then m.assert("write driving.qla"); end
warn("RECOVER FROM DISK FULL");
--m.setstep(false);

m.delete("eating.qla");

--m.speed(1);
st=m.write("setal.qla", chunk);
if (st~=0) then m.assert("write setal.qla"); end
--m.speed(0);
m.delete("falcon.qla");

m.setmode("read");
--m.speed(0);
m.verify("metro.qla");
if (st~=0) then m.assert("verify metro.qla"); end
m.verify("swim.qla");
if (st~=0) then m.assert("verify swim.qla"); end
m.verify("bench.qla");
if (st~=0) then m.assert("verify bench.qla"); end

#include "Epidemic/GameFramework/RolesJobs/roles_jobs.h"
#include <cstdlib>
#include <iostream>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::roles_jobs;

namespace
{
void Check(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
GameplayObjectRef Ref(const char* domain,const char* id){return {GameplayDomainId::FromString(domain),GameplayObjectId::FromString(id)};}
}

int main()
{
    RolesJobsService service;
    JobDefinition job;
    job.id=JobDefinitionId::FromString("job.shopkeeper");
    job.function=RoleFunctionId::FromString("function.trade");
    job.default_tasks.push_back(JobTaskTypeId::FromString("task.open_shop"));
    Check(static_cast<bool>(service.RegisterJobDefinition(job)),"register job");

    Workplace workplace;
    workplace.area=Ref("world.area","market");
    workplace.property=Ref("property","shop");
    auto workplace_id=service.CreateWorkplace(workplace);
    Check(static_cast<bool>(workplace_id),"create workplace");

    JobAssignment assignment;
    assignment.worker=Ref("entities","npc.shopkeeper");
    assignment.job=job.id;
    assignment.workplace=workplace_id.Value();
    auto assignment_id=service.AssignJob(assignment,{.time=GameplayTimePoint{0}});
    Check(static_cast<bool>(assignment_id),"assign job");

    WorkSchedule schedule;
    schedule.assignment=assignment_id.Value();
    schedule.priority=10;
    WorkShift shift;
    shift.start=GameplayTimePoint{800};
    shift.duration=GameplayDuration{400};
    shift.task_type=JobTaskTypeId::FromString("task.open_shop");
    shift.target_area=workplace.area;
    schedule.shifts.push_back(shift);
    Check(static_cast<bool>(service.CreateSchedule(schedule)),"create schedule");
    Check(service.ActivateDueShifts(GameplayTimePoint{700}).empty(),"not due");
    auto duties=service.ActivateDueShifts(GameplayTimePoint{900});
    Check(duties.size()==1,"shift due");
    Check(service.GetCurrentDuty(assignment.worker)!=nullptr,"current duty");
    Check(static_cast<bool>(service.CompleteDuty(duties.front())),"complete duty");
    Check(service.FindActiveDuties().empty(),"duty completed");

    Check(static_cast<bool>(service.SetWorkplaceState(workplace_id.Value(),WorkplaceState::Closed)),"close workplace");
    auto snapshot=service.CaptureSnapshot();
    RolesJobsService restored;
    Check(static_cast<bool>(restored.RestoreSnapshot(snapshot)),"restore");
    Check(restored.GetWorkplace(workplace_id.Value())->state==WorkplaceState::Closed,"restore workplace state");
    Check(restored.FindJobsOfSubject(assignment.worker).size()==1,"restore assignment");
    return 0;
}

#include <unistd.h>
#include <qsqldatabase.h>
#include <qsqlquery.h>
#include <qstring.h>
#include <qdatetime.h>

#include <iostream>
#include <algorithm>
using namespace std;

#include "scheduler.h"

#include "libmyth/programinfo.h"

Scheduler::Scheduler(QSqlDatabase *ldb)
{
    hasconflicts = false;
    db = ldb;

    setupCards();
}

Scheduler::~Scheduler()
{
    while (recordingList.size() > 0)
    {
        ProgramInfo *pginfo = recordingList.back();
        delete pginfo;
        recordingList.pop_back();
    }
}

void Scheduler::setupCards(void)
{
    QSqlQuery query, subquery;
    QString thequery;

    thequery = "SELECT NULL FROM capturecard;";

    query = db->exec(thequery);

    numcards = -1;

    if (query.isActive())
        numcards = query.numRowsAffected();

    if (numcards <= 0)
    {
        cerr << "ERROR: no capture cards are defined in the database.\n";
        exit(0);
    }

    thequery = "SELECT sourceid,name FROM videosource ORDER BY sourceid;";

    query = db->exec(thequery);

    numsources = -1;

    if (query.isActive())
    {
        numsources = query.numRowsAffected();

        int source = 0;

        while (query.next())
        {
            source = query.value(0).toInt();

            thequery = QString("SELECT cardinputid FROM cardinput WHERE "
                               "sourceid = %1 ORDER BY cardinputid;")
                              .arg(source);
            subquery = db->exec(thequery);
            
            if (subquery.isActive() && subquery.numRowsAffected() > 0)
            {
                numInputsPerSource[source] = subquery.numRowsAffected();
 
                while (subquery.next())
                    sourceToInput[source].push_back(subquery.value(0).toInt());
            }
            else
            {
                numInputsPerSource[source] = -1;
                cerr << query.value(1).toString() << " is defined, but isn't "
                     << "attached to a cardinput.\n";
            }
        }
    }

    if (numsources <= 0)
    {
        cerr << "ERROR: No channel sources defined in the database\n";
        exit(0);
    }

    thequery = "SELECT cardid,cardinputid FROM cardinput ORDER BY cardinputid;";

    query = db->exec(thequery);

    if (query.isActive() && query.numRowsAffected() > 0)
    {
        while (query.next())
        {
            inputToCard[query.value(1).toInt()] = query.value(0).toInt();
        }  
    }
}

bool Scheduler::CheckForChanges(void)
{
    QSqlQuery query;
    QString thequery;
    
    bool retval = false;

    thequery = "SELECT data FROM settings WHERE value = \"RecordChanged\";";
    query = db->exec(thequery);

    if (query.isActive() && query.numRowsAffected() > 0)
    {
        query.next();

        QString value = query.value(0).toString();
        if (value == "yes")
        {
            thequery = "UPDATE settings SET data = \"no\" WHERE value = "
                       "\"RecordChanged\";";
            query = db->exec(thequery);

            retval = true;
        }
    }

    return retval;
}

class comp_proginfo 
{
  public:
    bool operator()(ProgramInfo *a, ProgramInfo *b)
    {
        return (a->startts < b->startts);
    }
};

bool Scheduler::FillRecordLists(bool doautoconflicts)
{
    while (recordingList.size() > 0)
    {
        ProgramInfo *pginfo = recordingList.back();
        delete pginfo;
        recordingList.pop_back();
    }

    QString thequery;
    QSqlQuery query;
    QSqlQuery subquery;

    QDateTime curTime = QDateTime::currentDateTime();

    thequery = "SELECT channel.chanid,sourceid,starttime,endtime,title,"
               "subtitle,description,channel.channum,channel.callsign,"
               "channel.name FROM singlerecord,channel "
               "WHERE channel.chanid = singlerecord.chanid;";

    query = db->exec(thequery);
    if (query.isActive() && query.numRowsAffected() > 0)
    {
        while (query.next())
        {
            ProgramInfo *proginfo = new ProgramInfo;
            proginfo->chanid = query.value(0).toString();
            proginfo->sourceid = query.value(1).toInt();
            proginfo->startts = QDateTime::fromString(query.value(2).toString(),
                                                      Qt::ISODate);
            proginfo->endts = QDateTime::fromString(query.value(3).toString(),
                                                    Qt::ISODate);
            proginfo->title = query.value(4).toString();
            proginfo->subtitle = query.value(5).toString();
            proginfo->description = query.value(6).toString();
            proginfo->chanstr = query.value(7).toString();
            proginfo->chansign = query.value(8).toString();
            proginfo->channame = query.value(9).toString();
            proginfo->recordtype = kSingleRecord;

            if (proginfo->title == QString::null)
                proginfo->title = "";
            if (proginfo->subtitle == QString::null)
                proginfo->subtitle = "";
            if (proginfo->description == QString::null)
                proginfo->description = "";

            if (proginfo->startts < curTime)
                delete proginfo;
            else 
                recordingList.push_back(proginfo);
        }
    }

    thequery = "SELECT channel.chanid,sourceid,starttime,endtime,title FROM "
               "timeslotrecord,channel WHERE "
               "channel.chanid = timeslotrecord.chanid;";

    query = db->exec(thequery);
    if (query.isActive() && query.numRowsAffected() > 0)
    {
        while (query.next())
        {
            QString chanid = query.value(0).toString();
            int sourceid = query.value(1).toInt();
            QString starttime = query.value(2).toString();
            QString endtime = query.value(3).toString();
            QString title = query.value(4).toString();

            if (title == QString::null)
                continue;

            int hour, min;

            hour = starttime.mid(0, 2).toInt();
            min = starttime.mid(3, 2).toInt();

            QDate curdate = QDate::currentDate();
            QTime curtime = QTime::currentTime();

            char startquery[128], endquery[128];

            for (int dateoffset = 0; dateoffset <= 7; dateoffset++)
            {
                sprintf(startquery, "%4d%02d%02d%02d%02d00", curdate.year(),
                        curdate.month(), curdate.day(), hour, min);

                curdate = curdate.addDays(1);
                sprintf(endquery, "%4d%02d%02d%02d%02d00", curdate.year(),
                        curdate.month(), curdate.day(), hour, min);

                if (dateoffset == 0 && (curtime.hour() > hour ||
                    (curtime.hour() == hour && curtime.minute() > min)))
                    continue;

                thequery = QString("SELECT channel.chanid,starttime,"
                                   "endtime,title,subtitle,description,"
                                   "channel.channum,channel.callsign,"
                                   "channel.name "
                                   "FROM program,channel WHERE "
                                   "program.chanid = %1 AND "
                                   "starttime = %2 AND endtime < %3 AND "
                                   "title = \"%4\" AND "
                                   "channel.chanid = program.chanid AND "
                                   "channel.sourceid = %5;") 
                                   .arg(chanid).arg(startquery).arg(endquery)
                                   .arg(title).arg(sourceid);
                subquery = db->exec(thequery);

                if (subquery.isActive() && subquery.numRowsAffected() > 0)
                {
                    while (subquery.next())
                    {
                        ProgramInfo *proginfo = new ProgramInfo;

                        proginfo->sourceid = sourceid;
 
                        proginfo->chanid = subquery.value(0).toString();
                        proginfo->startts = 
                             QDateTime::fromString(subquery.value(1).toString(),
                                                   Qt::ISODate);
                        proginfo->endts = 
                             QDateTime::fromString(subquery.value(2).toString(),
                                                   Qt::ISODate);
                        proginfo->title = subquery.value(3).toString();
                        proginfo->subtitle = subquery.value(4).toString();
                        proginfo->description = subquery.value(5).toString();
                        proginfo->chanstr = subquery.value(6).toString();
                        proginfo->chansign = subquery.value(7).toString();
                        proginfo->channame = subquery.value(8).toString();
                        proginfo->recordtype = kTimeslotRecord;

                        if (proginfo->title == QString::null)
                            proginfo->title = "";
                        if (proginfo->subtitle == QString::null)
                            proginfo->subtitle = "";
                        if (proginfo->description == QString::null)
                            proginfo->description = "";

                        recordingList.push_back(proginfo);
                    }
                }
            }
        }
    }

    thequery = "SELECT title,chanid FROM allrecord;";
    query = db->exec(thequery);

    if (query.isActive() && query.numRowsAffected() > 0)
    {
        while (query.next())
        {
            QString title = query.value(0).toString();
            int chanid = query.value(1).toInt();   

            if (title == QString::null)
                continue;

            QTime curtime = QTime::currentTime();
            QDate curdate = QDate::currentDate();
            char startquery[128], endquery[128];

            sprintf(startquery, "%4d%02d%02d%02d%02d00", curdate.year(),
                    curdate.month(), curdate.day(), curtime.hour(), 
                    curtime.minute());

            curdate = curdate.addDays(7);
            sprintf(endquery, "%4d%02d%02d%02d%02d00", curdate.year(),
                    curdate.month(), curdate.day(), curtime.hour(), 
                    curtime.minute());

            thequery = QString("SELECT channel.chanid,sourceid,starttime,"
                               "endtime,title,subtitle,description,"
                               "channel.channum,channel.callsign,channel.name "
                               "FROM program,channel WHERE starttime >= %1 AND "
                               "endtime < %2 AND title = \"%3\" AND "
                               "channel.chanid = program.chanid")
                               .arg(startquery).arg(endquery).arg(title);

            if (chanid > 0)
                thequery += QString(" AND channel.chanid = %1").arg(chanid);
            thequery += ";";

            subquery = db->exec(thequery);

            if (subquery.isActive() && subquery.numRowsAffected() > 0)
            {
                while (subquery.next())
                {
                    ProgramInfo *proginfo = new ProgramInfo;

                    proginfo->chanid = subquery.value(0).toString();
                    proginfo->sourceid = subquery.value(1).toInt();
                    proginfo->startts = 
                             QDateTime::fromString(subquery.value(2).toString(),
                                                   Qt::ISODate);
                    proginfo->endts = 
                             QDateTime::fromString(subquery.value(3).toString(),
                                                   Qt::ISODate);
                    proginfo->title = subquery.value(4).toString();
                    proginfo->subtitle = subquery.value(5).toString();
                    proginfo->description = subquery.value(6).toString();
                    proginfo->chanstr = subquery.value(7).toString();
                    proginfo->chansign = subquery.value(8).toString();
                    proginfo->channame = subquery.value(9).toString();

                    if (chanid > 0)
                        proginfo->recordtype = kChannelRecord;
                    else
                        proginfo->recordtype = kAllRecord;

                    if (proginfo->title == QString::null)
                        proginfo->title = "";
                    if (proginfo->subtitle == QString::null)
                        proginfo->subtitle = "";
                    if (proginfo->description == QString::null)
                        proginfo->description = "";

                    recordingList.push_back(proginfo);
                }
            }
        }
    }

    if (recordingList.size() > 0)
    {
        recordingList.sort(comp_proginfo());
        MarkKnownInputs();
        MarkConflicts();
        PruneList(); 
        MarkConflicts();

        if (numcards > 1)
        {
            DoMultiCard();
            MarkConflicts();
        }

        MarkConflictsToRemove();
        if (doautoconflicts)
        {
            RemoveConflicts();
            GuessConflicts();
            RemoveConflicts();
        }
        MarkConflicts();
    }

    return hasconflicts;
}

void Scheduler::PrintList(void)
{
    list<ProgramInfo *>::iterator i = recordingList.begin();
    for (; i != recordingList.end(); i++)
    {
        ProgramInfo *first = (*i);
        cout << first->title << " " << first->chanstr << " " << first->chanid << " " << first->recordtype << " \"" << first->startts.toString() << "\" " << first->sourceid << " " << first->inputid << " " << first->cardid << " --\t"  << first->conflicting << " " << first->recording << endl;
    }

    cout << endl << endl;
}

ProgramInfo *Scheduler::GetNextRecording(void)
{
    if (recordingList.size() > 0)
        return recordingList.front();
    return NULL;
}

void Scheduler::RemoveFirstRecording(void)
{
    if (recordingList.size() == 0)
        return;

    ProgramInfo *rec = recordingList.front();

    delete rec;
    recordingList.pop_front();
}

bool Scheduler::Conflict(ProgramInfo *a, ProgramInfo *b)
{
    if (a->cardid > 0 && b->cardid > 0)
    {
        if (a->cardid != b->cardid)
            return false;
    }

    if ((a->startts <= b->startts && b->startts < a->endts) ||
        (a->startts <  b->endts   && b->endts   < a->endts) ||
        (b->startts <= a->startts && a->startts < b->endts) ||
        (b->startts <  a->endts   && a->endts   < b->endts))
        return true;
    return false;
}

void Scheduler::MarkKnownInputs(void)
{
    list<ProgramInfo *>::iterator i = recordingList.begin();
    for (; i != recordingList.end(); i++)
    {
        ProgramInfo *first = (*i);
        if (first->inputid == -1)
        {
            if (numInputsPerSource[first->sourceid] == 1)
            {
                first->inputid = sourceToInput[first->sourceid][0];
                first->cardid = inputToCard[first->inputid];
            }
        }
    }
}
 
void Scheduler::MarkConflicts(list<ProgramInfo *> *uselist)
{
    list<ProgramInfo *> *curList = &recordingList;
    if (uselist)
        curList = uselist;

    hasconflicts = false;
    list<ProgramInfo *>::iterator i = curList->begin();
    for (; i != curList->end(); i++)
    {
        ProgramInfo *first = (*i);
        first->conflicting = false;
    }

    for (i = curList->begin(); i != curList->end(); i++)
    {
        list<ProgramInfo *>::iterator j = i;
        j++;
        for (; j != curList->end(); j++)
        {
            ProgramInfo *first = (*i);
            ProgramInfo *second = (*j);

            if (!first->recording || !second->recording)
                continue;
            if (Conflict(first, second))
            {
                first->conflicting = true;
                second->conflicting = true;
                hasconflicts = true;
            }
        }
    }
}

bool Scheduler::FindInOldRecordings(ProgramInfo *pginfo)
{
    QSqlQuery query;
    QString thequery;
   
    if (pginfo->subtitle == "" || pginfo->description == "")
        return false;

    thequery = QString("SELECT NULL FROM oldrecorded WHERE "
                       "title = \"%1\" AND subtitle = \"%2\" AND "
                       "description = \"%3\";").arg(pginfo->title)
                       .arg(pginfo->subtitle).arg(pginfo->description);

    query = db->exec(thequery);

    if (query.isActive() && query.numRowsAffected() > 0)
        return true;
    return false;
}

void Scheduler::PruneList(void)
{
    list<ProgramInfo *>::reverse_iterator i = recordingList.rbegin();
    list<ProgramInfo *>::iterator deliter;

    i = recordingList.rbegin();
    while (i != recordingList.rend())
    {
        list<ProgramInfo *>::reverse_iterator j = i;
        j++;

        ProgramInfo *first = (*i);

        if (first->recordtype > kSingleRecord && 
            (first->subtitle.length() > 2 || first->description.length() > 2))
        {
            if (FindInOldRecordings(first))
            {
                delete first;
                deliter = i.base();
                deliter--;
                recordingList.erase(deliter);
            }
            else
            {
                for (; j != recordingList.rend(); j++)
                {
                    ProgramInfo *second = (*j);
                    if ((first->title == second->title) && 
                        (first->subtitle == second->subtitle) &&
                        (first->description == second->description) &&
                        first->subtitle != "" && first->description != "")
                    {
                        if (second->conflicting && !first->conflicting)
                        {
                            delete second;
                            deliter = j.base();
                            j++;
                            deliter--;
                            recordingList.erase(deliter);
                        }
                        else
                        {
                            delete first;
                            deliter = i.base();
                            deliter--;
                            recordingList.erase(deliter);
                            break;
                        }
                    }
                }
            }
        }
        i++;
    }    
}

list<ProgramInfo *> *Scheduler::getConflicting(ProgramInfo *pginfo,
                                               bool removenonplaying,
                                               list<ProgramInfo *> *uselist)
{
    if (!pginfo->conflicting && removenonplaying)
        return NULL;

    list<ProgramInfo *> *curList = &recordingList;
    if (uselist)
        curList = uselist;

    list<ProgramInfo *> *retlist = new list<ProgramInfo *>;

    list<ProgramInfo *>::iterator i = curList->begin();
    for (; i != curList->end(); i++)
    {
        ProgramInfo *second = (*i);

        if (second->title == pginfo->title && 
            second->startts == pginfo->startts &&
            second->chanid == pginfo->chanid)
            continue;

        if (removenonplaying && (!pginfo->recording || !second->recording))
            continue;
        if (Conflict(pginfo, second))
            retlist->push_back(second);
    }

    return retlist;
}

void Scheduler::CheckOverride(ProgramInfo *info,
                              list<ProgramInfo *> *conflictList)
{
    QSqlQuery query;
    QString thequery;

    QString starts = info->startts.toString("yyyyMMddhhmm");
    starts += "00";
    QString ends = info->endts.toString("yyyyMMddhhmm");
    ends += "00";

    thequery = QString("SELECT NULL FROM conflictresolutionoverride WHERE "
                       "chanid = %1 AND starttime = %2 AND "
                       "endtime = %3;").arg(info->chanid).arg(starts)
                       .arg(ends);

    query = db->exec(thequery);

    if (query.isActive() && query.numRowsAffected() > 0)
    {
        query.next();
        
        list<ProgramInfo *>::iterator i = conflictList->begin();
        for (; i != conflictList->end(); i++)
        {
            ProgramInfo *del = (*i);

            del->recording = false;
        }
        info->conflicting = false;
    }
}

void Scheduler::MarkSingleConflict(ProgramInfo *info,
                                   list<ProgramInfo *> *conflictList)
{
    QSqlQuery query;
    QString thequery;

    list<ProgramInfo *>::iterator i;

    QString starts = info->startts.toString("yyyyMMddhhmm");
    starts += "00";
    QString ends = info->endts.toString("yyyyMMddhhmm");
    ends += "00";
  
    thequery = QString("SELECT dislikechanid,dislikestarttime,dislikeendtime "
                       "FROM conflictresolutionsingle WHERE "
                       "preferchanid = %1 AND preferstarttime = %2 AND "
                       "preferendtime = %3;").arg(info->chanid)
                       .arg(starts).arg(ends);
 
    query = db->exec(thequery);

    if (query.isActive() && query.numRowsAffected() > 0)
    {
        while (query.next())
        {
            QString badchannum = query.value(0).toString();
            QDateTime badst = QDateTime::fromString(query.value(1).toString(),
                                                    Qt::ISODate);
            QDateTime badend = QDateTime::fromString(query.value(2).toString(),
                                                     Qt::ISODate);

            i = conflictList->begin();
            for (; i != conflictList->end(); i++)
            {
                ProgramInfo *test = (*i);
                if (test->chanid == badchannum && test->startts == badst && 
                    test->endts == badend)
                {
                    test->recording = false;
                }
            }
        }
    }

    bool conflictsleft = false;
    i = conflictList->begin();
    for (; i != conflictList->end(); i++)
    {
        ProgramInfo *test = (*i);
        if (test->recording == true)
            conflictsleft = true;
    }

    if (!conflictsleft)
    {
        info->conflicting = false;
        return;
    }

    thequery = QString("SELECT disliketitle FROM conflictresolutionany WHERE "
                       "prefertitle = \"%1\";").arg(info->title);

    query = db->exec(thequery);

    if (query.isActive() && query.numRowsAffected() > 0)
    {
        while (query.next())
        {
            QString badtitle = query.value(0).toString();

            i = conflictList->begin();
            for (; i != conflictList->end(); i++)
            {
                ProgramInfo *test = (*i);
                if (test->title == badtitle)
                {
                    test->recording = false;
                }
            }
        }
    }
    
    conflictsleft = false;
    i = conflictList->begin();
    for (; i != conflictList->end(); i++)
    {
        ProgramInfo *test = (*i);
        if (test->recording == true)
            conflictsleft = true;
    }

    if (!conflictsleft)
    {
        info->conflicting = false;
    }
}

void Scheduler::MarkConflictsToRemove(void)
{
    list<ProgramInfo *>::iterator i = recordingList.begin();
    for (; i != recordingList.end(); i++)
    {
        ProgramInfo *first = (*i);
    
        if (first->conflicting && first->recording)
        {
            list<ProgramInfo *> *conflictList = getConflicting(first);
            CheckOverride(first, conflictList); 
            delete conflictList;
        }
    }

    i = recordingList.begin();
    for (; i != recordingList.end(); i++)
    {
        ProgramInfo *first = (*i);
  
        if (first->conflicting && first->recording)
        {
            list<ProgramInfo *> *conflictList = getConflicting(first);
            MarkSingleConflict(first, conflictList);
            delete conflictList;
        }
        else if (!first->recording)
            first->conflicting = false;
    }
}

void Scheduler::RemoveConflicts(void)
{
    list<ProgramInfo *>::iterator del;
    list<ProgramInfo *>::iterator i = recordingList.begin();
    while (i != recordingList.end())
    {
        ProgramInfo *first = (*i);

        del = i;
        i++;

        if (!first->recording)
        {
            delete first;
            recordingList.erase(del);
        }
    }
}

ProgramInfo *Scheduler::GetBest(ProgramInfo *info, 
                                list<ProgramInfo *> *conflictList)
{
    RecordingType type = info->recordtype;
    ProgramInfo *best = info;

    list<ProgramInfo *>::iterator i;
    for (i = conflictList->begin(); i != conflictList->end(); i++)
    {
        ProgramInfo *test = (*i);
        if (test->recordtype < type)
        {
            best = test;
            type = test->recordtype;
            break;
        }
        else if (test->recordtype == type)
        {
            if (test->startts < info->startts)
            {
                best = test;
                break;
            }
            if (test->startts.secsTo(test->endts) >
                info->startts.secsTo(info->endts))
            {
                best = test;
                break;
            }
            if (test->chanid.toInt() < info->chanid.toInt())
            {
                best = test;
                break;
            }
        }
    }

    return best;
}

void Scheduler::GuessSingle(ProgramInfo *info, 
                            list<ProgramInfo *> *conflictList)
{
    ProgramInfo *best = info;
    list<ProgramInfo *>::iterator i;
 
    if (conflictList->size() == 0)
    {
        info->conflicting = false;
        return;
    }

    best = GetBest(info, conflictList);

    if (best == info)
    {
        for (i = conflictList->begin(); i != conflictList->end(); i++)
        {
            ProgramInfo *pginfo = (*i);
            pginfo->recording = false;
        }
        best->conflicting = false;
    }
    else
    {
        info->recording = false;
    } 
}

void Scheduler::GuessConflicts(void)
{
    list<ProgramInfo *>::iterator i = recordingList.begin();
    for (; i != recordingList.end(); i++)
    {
        ProgramInfo *first = (*i);
        if (first->recording && first->conflicting)
        {
            list<ProgramInfo *> *conflictList = getConflicting(first);
            GuessSingle(first, conflictList);
            delete conflictList;
        }
    }
}

list<ProgramInfo *> *Scheduler::CopyList(list<ProgramInfo *> *sourcelist)
{
    list<ProgramInfo *> *retlist = new list<ProgramInfo *>;

    list<ProgramInfo *>::iterator i = sourcelist->begin();
    for (; i != sourcelist->end(); i++)
    {
        ProgramInfo *first = (*i);
        ProgramInfo *second = new ProgramInfo(*first);

        second->conflictfixed = false;

        if (second->cardid <= 0)
        {
            second->inputid = sourceToInput[first->sourceid][0];
            second->cardid = inputToCard[second->inputid];
        }

        retlist->push_back(second);
    }

    return retlist;
}
        
void Scheduler::DoMultiCard(void)
{
    list<ProgramInfo *> *copylist = CopyList(&recordingList);

    MarkConflicts(copylist);

    int numconflicts = 0;

    list<ProgramInfo *> allConflictList;
    list<bool> canMoveList;

    list<ProgramInfo *>::iterator i;
    for (i = copylist->begin(); i != copylist->end(); i++)
    {
        ProgramInfo *first = (*i);
        if (first->recording && first->conflicting)
        {
            numconflicts++;
            allConflictList.push_back(first);
            if (numInputsPerSource[first->sourceid] == 1) 
                canMoveList.push_back(false);
            else
                canMoveList.push_back(true);
        }
    }

    list<bool>::iterator biter;
    for (biter = canMoveList.begin(), i = allConflictList.begin(); 
         biter != canMoveList.end(); biter++, i++)
    {
        ProgramInfo *first = (*i);

        list<ProgramInfo *> *conflictList = getConflicting(first, true, 
                                                           copylist);

        bool firstmove = *biter;

        list<ProgramInfo *>::iterator j = conflictList->begin();
        for (; j != conflictList->end(); j++)
        {
            ProgramInfo *second = (*j);

            bool secondmove = (numInputsPerSource[second->sourceid] > 1);

            if (second->conflictfixed)
                secondmove = false;

            bool fixed = false;
            if (secondmove)
            {
                int storeinput = second->inputid;
                int numinputs = numInputsPerSource[second->sourceid];

                for (int z = 0; z < numinputs; z++)
                {
                    second->inputid = sourceToInput[second->sourceid][z];
                    second->cardid = inputToCard[second->inputid];

                    if (!Conflict(first, second))
                    {
                        fixed = true;
                        break;
                    }
                }
                if (!fixed)
                {
                    second->inputid = storeinput;
                    second->cardid = inputToCard[second->inputid];
                }
            }

            if (!fixed && firstmove)
            {
                int storeinput = first->inputid;
                int numinputs = numInputsPerSource[first->sourceid];

                for (int z = 0; z < numinputs; z++)
                {
                    first->inputid = sourceToInput[first->sourceid][z];
                    first->cardid = inputToCard[first->inputid];

                    if (!Conflict(first, second))
                    {
                        fixed = true;
                        break;
                    }
                }
                if (!fixed)
                {
                    first->inputid = storeinput;
                    first->cardid = inputToCard[first->inputid];
                }
            }
        }

        delete conflictList;
        conflictList = getConflicting(first, true, copylist);
        if (!conflictList || conflictList->size() == 0)
            first->conflictfixed = true;

        delete conflictList;
    }


    for (i = recordingList.begin(); i != recordingList.end(); i++)
    {
        ProgramInfo *first = (*i);
        delete first;
    }

    recordingList.clear();

    for (i = copylist->begin(); i != copylist->end(); i++)
    {
        ProgramInfo *first = (*i);
        recordingList.push_back(first);
    }

    delete copylist;
}

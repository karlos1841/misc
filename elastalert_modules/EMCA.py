from elastalert.ruletypes import RuleType
from elastalert.ruletypes import CompareRule

import datetime

from elastalert.util import elastalert_logger
from elastalert.util import total_seconds
from elastalert.util import lookup_es_key
from elastalert.util import EAException
from elastalert.util import elasticsearch_client

class ConsecutiveGrowthRule(CompareRule):
    required_options = frozenset(['query_key', 'compare_key'])

    def __init__(self, *args):
        super(ConsecutiveGrowthRule, self).__init__(*args)
        # dict with query_key as key and list of last count compare_key values as value
        self.arr = {}
        # by default last 3 values are checked
        self.count = 3
        if 'count' in self.rules:
            self.count = self.rules['count']

    def set_off_alarm(self, qkey):
        if len(self.arr[qkey]) < self.count:
            return False
        for i in range(len(self.arr[qkey])-1):
            if self.arr[qkey][i+1] <= self.arr[qkey][i]:
                return False
        return True

    def add_value(self, qkey, value):
        try:
            self.arr[qkey].append(float(value))
        except ValueError:
            elastalert_logger.info("Ignoring value since it is not a number")
            return False
        while len(self.arr[qkey]) > self.count:
            self.arr[qkey].pop(0)
        return True

    def compare(self, event):
        changed = False
        qkey = lookup_es_key(event, self.rules['query_key'])
        value = lookup_es_key(event, self.rules['compare_key'])
        if qkey not in self.arr:
            self.arr[qkey] = []

        if self.add_value(qkey, value):
            changed = self.set_off_alarm(qkey)

        elastalert_logger.info("%s: %s" % (str(qkey), str(self.arr[qkey])))
        return changed



class FindMatchRule(RuleType):
    required_options = frozenset(['query_key', 'compare_key', 'start_value', 'end_value', 'timeframe', 'invert'])

    def __init__(self, *args):
        super(FindMatchRule, self).__init__(*args)
        self.compare_key = self.rules['compare_key']
        self.start_value = self.rules['start_value']
        self.end_value = self.rules['end_value']
        self.query_key = self.rules['query_key']
        self.time_seconds = total_seconds(self.rules['timeframe'])
        self.invert = self.rules['invert']

        # dict with query_key as key and timestamp from start event as value
        self.start_time_per_qk = {}

        # list of end events, needed to remove matching start event if it comes after end event
        self.end_event_qk = []

    def garbage_collect(self, timestamp):
        key_del = set()
        for key, value in self.start_time_per_qk.items():
            if total_seconds(timestamp - value) > self.time_seconds:
                # delete start entry if end event not found within specified timeframe
                if not self.invert:
                    elastalert_logger.info("Match not found for query_key: %s within specified timeframe" % key)
                    key_del.add(key)

                # alert when end event not found after specified time has elapsed
                else:
                    elastalert_logger.info("Alert triggered! End event with query_key: %s not found" % key)
                    extra = {
                        self.compare_key: 'end event not found',
                        'start_event': self.start_value,
                        'end_event': self.end_value,
                        self.query_key: key,
                    }
                    self.add_match(extra)
                    key_del.add(key)

        for key in key_del:
            del self.start_time_per_qk[key]

    def add_data(self, data):
        # sort list
        start_events = []
        end_events = []
        for d in data:
            if self.compare_key in d and self.query_key in d:
                if d[self.compare_key] == self.start_value:
                    start_events.append(d)
                elif d[self.compare_key] == self.end_value:
                    end_events.append(d)

        data = start_events + end_events

        # loop through sorted list
        for d in data:

            # if this event is start event then fill dict with this event's query_key and timestamp
            # we ignore any further occurrences of start events with the same query_key
            if d[self.compare_key] == self.start_value:
                if d[self.query_key] not in self.start_time_per_qk:
                    # if end event did not come before start event then add entry for start event
                    if d[self.query_key] not in self.end_event_qk:
                        self.start_time_per_qk[d[self.query_key]] = d['@timestamp']
                    # if end event is in the list before start event comes then remove end event entry
                    else:
                        self.end_event_qk.remove(d[self.query_key])

            # if this event is end event then check if its query_key is present in dict containing start events
            elif d[self.compare_key] == self.end_value:

                # we search for matches
                if not self.invert:
                    # if this event does not have corresponding start event then either we missed one or some error occurred
                    if d[self.query_key] not in self.start_time_per_qk:
                        elastalert_logger.info("Found the last event with query_key: %s without match" % str(d[self.query_key]))
                    else:
                        # if time difference between the events is within specified time then it is an alert
                        diff = total_seconds(d['@timestamp'] - self.start_time_per_qk[d[self.query_key]])
                        elastalert_logger.info("Found match for query_key: %s, "
                                               "time gap between the events in seconds: %s" % (str(d[self.query_key]), str(diff)))
                        if diff <= self.time_seconds:
                            elastalert_logger.info("Alert triggered for events with query_key: %s" % str(d[self.query_key]))
                            extra = {
                                'start_event': self.start_value,
                                'end_event': self.end_value,
                                'elapsed_time': diff,
                            }
                            self.add_match(dict(list(d.items()) + list(extra.items())))

                        del self.start_time_per_qk[d[self.query_key]]

                # we search for non-matched events so let's delete start entry if end event is found
                else:
                    if d[self.query_key] in self.start_time_per_qk:
                        diff = total_seconds(d['@timestamp'] - self.start_time_per_qk[d[self.query_key]])
                        elastalert_logger.info("Found match for query_key: %s, "
                                               "time gap between the events in seconds: %s" % (str(d[self.query_key]), str(diff)))
                        # alert when end event is found but not within specified time
                        if diff > self.time_seconds:
                            elastalert_logger.info("Alert triggered for events with query_key: %s" % str(d[self.query_key]))
                            extra = {
                                'start_event': self.start_value,
                                'end_event': self.end_value,
                                'elapsed_time': diff,
                            }
                            self.add_match(dict(list(d.items()) + list(extra.items())))

                        del self.start_time_per_qk[d[self.query_key]]
                    # add entry for end event if matching start event not found (maybe end event comes first)
                    else:
                        if d[self.query_key] not in self.end_event_qk:
                            self.end_event_qk.append(d[self.query_key])



class UniqueLongTermRule(RuleType):
    required_options = frozenset(['compare_key', 'no_of_timeperiods', 'timeframe'])

    def __init__(self, *args):
        super(UniqueLongTermRule, self).__init__(*args)
        self.values = []
        self.garbage_time = 0
        self.exec_num = 0
        self.field = self.rules['compare_key']
        self.timeperiods_index = 0
        self.no_of_timeperiods = int(self.rules['no_of_timeperiods'])
        for i in range(0, self.no_of_timeperiods):
            self.values.append(set())

        timeperiod_sec = int(self.rules['timeframe'].total_seconds())
        run_every_sec = int(self.rules['run_every'].total_seconds())
        if run_every_sec > timeperiod_sec:
            raise EAException("Run Every option cannot be greater than Timeperiod option")
        if timeperiod_sec % run_every_sec != 0:
            raise EAException("Run Every must fit integer number of times in Timeperiod")
        self.runs_per_timeperiod = int(timeperiod_sec / run_every_sec)

        elastalert_logger.info(
            "Timeperiod sec: %d, Number of executions per timeperiod: %d, Number of timeperiods: %d" % (
                timeperiod_sec, self.runs_per_timeperiod, self.no_of_timeperiods))

    def garbage_collect(self, timestamp):
        if self.garbage_time == 0:
            self.garbage_time = timestamp
        diff = timestamp - self.garbage_time
        self.garbage_time = timestamp
        elastalert_logger.info("From garbage collect - time diff since last exec: %f" % diff.total_seconds())
        if not diff.total_seconds() > 0:
            return

        self.exec_num += 1
        elastalert_logger.info("Timeperiod: %d/%d" % (self.exec_num, self.runs_per_timeperiod))
        # end of timeperiod
        if self.exec_num >= self.runs_per_timeperiod:
            self.exec_num = 0
            self.timeperiods_index += 1
        # end of all timeperiods (self.no_of_timeperiods)
        if self.timeperiods_index >= self.no_of_timeperiods:
            elastalert_logger.info("All timeperiods passed")
            elastalert_logger.info("Sets for all timeperiods: %s" % str(self.values))
            self.timeperiods_index = 0
            result = self.values[0]
            for i in range(0, self.no_of_timeperiods):
                result = result & self.values[i]
                self.values[i].clear()

            if result != set():
                result_str = " ".join(result)
                elastalert_logger.info("Alert triggered, final result: %s" % result_str)
                extra = {self.field: result_str}
                self.add_match(extra)

    def add_data(self, data):
        for d in data:
            try:
                self.values[self.timeperiods_index].add(d[self.field])
            except KeyError:
                pass



class DifferenceRule(RuleType):
    required_options = frozenset(['compare_key', 'query_key', 'threshold_pct', 'delta_min', 'agg_min'])

    def __init__(self, *args):
        super(DifferenceRule, self).__init__(*args)
        # self.diff_key = self.rules['compare_key'].split('.')
        self.diff_key = self.rules['compare_key']
        self.threshold_pct = self.rules['threshold_pct']
        self.delta_sec = self.rules['delta_min'] * 60
        self.agg_sec = self.rules['agg_min'] * 60
        self.qkey = self.rules['query_key']
        # keys are query_key values and values are objects of inner class
        # self.qobj = {}
        self.include = self.rules['include']
        # do not include @timestamp
        self.include = [i for i in self.include if i != '@timestamp']
        # self.include_all = []
        # if 'include_all' in self.rules:
        #	self.include_all = self.rules['include_all']
        #	self.include = list(set(self.include) - set(self.include_all))

        # set realert to 0 to get alert for each query_key in one minute
        # since this query_key is not part of core elastalert
        self.rules['realert'] = datetime.timedelta(minutes=0)
        if not self.delta_sec >= self.agg_sec:
            raise EAException("delta_min must be greater or equal to agg_min")

        self.es = elasticsearch_client(self.rules)

        self.filter_query = {"query_string": {"query": "*"}}
        if self.rules['filter']:
            self.filter_query = self.rules['filter'][0]

    def get_epoch(self, ts):
        # convert timestamp from this event to seconds since epoch
        # get rid of timezone offset and milliseconds
        timestamp = str(ts).rsplit('+', 1)[0]
        timestamp = timestamp.rsplit('.', 1)[0]
        timestamp = timestamp.replace("T", " ", 1)
        utc_time = datetime.datetime.strptime(timestamp, '%Y-%m-%d %H:%M:%S')
        epoch = (utc_time - datetime.datetime(1970, 1, 1)).total_seconds()

        return epoch

    def garbage_collect(self, timestamp):
        qobj = {}
        epoch = self.get_epoch(timestamp)

        result_now = self.es.search(index=self.rules['index'], body={"size": 0, "query": {
            "bool": {"filter": self.filter_query, "must": [{"range": {
                "@timestamp": {"gte": str(int(epoch - self.agg_sec)), "lte": str(int(epoch)),
                               "format": "epoch_second"}}}]}}, "aggs": {
            "buckets": {"composite": {"sources": [{"query_key": {"terms": {"field": self.qkey}}}]},
                        "aggregations": {"avg": {"avg": {"field": self.diff_key}}}}}})['aggregations']['buckets'][
            'buckets']

        result_history = self.es.search(index=self.rules['index'], body={"size": 0, "query": {
            "bool": {"filter": self.filter_query, "must": [{"range": {
                "@timestamp": {"gte": str(int(epoch - self.agg_sec - self.delta_sec)),
                               "lte": str(int(epoch - self.delta_sec)), "format": "epoch_second"}}}]}}, "aggs": {
            "buckets": {"composite": {"sources": [{"query_key": {"terms": {"field": self.qkey}}}]},
                        "aggregations": {"avg": {"avg": {"field": self.diff_key}}}}}})['aggregations']['buckets'][
            'buckets']

        elastalert_logger.info("present time window: %s" % str(result_now))
        elastalert_logger.info("past time window: %s" % str(result_history))

        for doc in result_now:
            if doc['key']['query_key'] not in qobj:
                qobj[doc['key']['query_key']] = self.Qkey(self)

            if doc['avg']['value'] is not None:
                qobj[doc['key']['query_key']].set_avg_now(doc['avg']['value'])

        for doc in result_history:
            if doc['key']['query_key'] not in qobj:
                qobj[doc['key']['query_key']] = self.Qkey(self)

            if doc['avg']['value'] is not None:
                qobj[doc['key']['query_key']].set_avg_history(doc['avg']['value'])

        for include in self.include:
            # if error then fallback to keyword field
            try:
                include_now = self.es.search(index=self.rules['index'],
                                             body={
                                                 "size": 0,
                                                 "query": {
                                                     "bool": {
                                                         "filter": self.filter_query,
                                                         "must": [
                                                             {
                                                                 "range": {
                                                                     "@timestamp": {
                                                                         "gte": str(int(epoch - self.agg_sec)),
                                                                         "lte": str(int(epoch)),
                                                                         "format": "epoch_second"
                                                                     }
                                                                 }
                                                             }
                                                         ]
                                                     }
                                                 },
                                                 "aggs": {
                                                     "buckets": {
                                                         "composite": {
                                                             "sources": [
                                                                 {
                                                                     "query_key": {
                                                                         "terms": {
                                                                             "field": self.qkey
                                                                         }
                                                                     }
                                                                 },
                                                                 {
                                                                     "include": {
                                                                         "terms": {
                                                                             "field": include
                                                                         }
                                                                     }
                                                                 }
                                                             ]
                                                         }
                                                     }
                                                 }
                                             }
                                             )['aggregations']['buckets']['buckets']

                include_history = self.es.search(index=self.rules['index'],
                                                 body={"size": 0,
                                                       "query": {
                                                           "bool": {
                                                               "filter": self.filter_query,
                                                               "must": [
                                                                   {
                                                                       "range": {
                                                                           "@timestamp": {
                                                                               "gte": str(int(
                                                                                   epoch - self.agg_sec - self.delta_sec)),
                                                                               "lte": str(int(epoch - self.delta_sec)),
                                                                               "format": "epoch_second"
                                                                           }
                                                                       }
                                                                   }
                                                               ]
                                                           }
                                                       },
                                                       "aggs": {
                                                           "buckets": {
                                                               "composite": {
                                                                   "sources": [
                                                                       {
                                                                           "query_key": {
                                                                               "terms": {
                                                                                   "field": self.qkey
                                                                               }
                                                                           }
                                                                       },
                                                                       {
                                                                           "include": {
                                                                               "terms": {
                                                                                   "field": include
                                                                               }
                                                                           }
                                                                       }
                                                                   ]
                                                               }
                                                           }
                                                       }
                                                       }
                                                 )['aggregations']['buckets']['buckets']

            except RequestError:
                include_now = self.es.search(index=self.rules['index'],
                                             body={
                                                 "size": 0,
                                                 "query": {
                                                     "bool": {
                                                         "filter": self.filter_query,
                                                         "must": [
                                                             {
                                                                 "range": {
                                                                     "@timestamp": {
                                                                         "gte": str(int(epoch - self.agg_sec)),
                                                                         "lte": str(int(epoch)),
                                                                         "format": "epoch_second"
                                                                     }
                                                                 }
                                                             }
                                                         ]
                                                     }
                                                 },
                                                 "aggs": {
                                                     "buckets": {
                                                         "composite": {
                                                             "sources": [
                                                                 {
                                                                     "query_key": {
                                                                         "terms": {
                                                                             "field": self.qkey + ".keyword"
                                                                         }
                                                                     }
                                                                 },
                                                                 {
                                                                     "include": {
                                                                         "terms": {
                                                                             "field": include + ".keyword"
                                                                         }
                                                                     }
                                                                 }
                                                             ]
                                                         }
                                                     }
                                                 }
                                             }
                                             )['aggregations']['buckets']['buckets']

                include_history = self.es.search(index=self.rules['index'],
                                                 body={
                                                     "size": 0,
                                                     "query": {
                                                         "bool": {
                                                             "filter": self.filter_query,
                                                             "must": [{
                                                                 "range": {
                                                                     "@timestamp": {
                                                                         "gte": str(int(
                                                                             epoch - self.agg_sec - self.delta_sec)),
                                                                         "lte": str(int(epoch - self.delta_sec)),
                                                                         "format": "epoch_second"
                                                                     }
                                                                 }
                                                             }]
                                                         }
                                                     },
                                                     "aggs": {
                                                         "buckets": {
                                                             "composite": {
                                                                 "sources": [{
                                                                     "query_key": {
                                                                         "terms": {
                                                                             "field": self.qkey + ".keyword"
                                                                         }
                                                                     }
                                                                 },
                                                                     {
                                                                         "include": {
                                                                             "terms": {
                                                                                 "field": include + ".keyword"
                                                                             }
                                                                         }
                                                                     }]
                                                             }
                                                         }
                                                     }
                                                 }
                                                 )['aggregations']['buckets']['buckets']

            for doc in include_now:
                if doc['key']['query_key'] in qobj:
                    qobj[doc['key']['query_key']].set_include_now(include, doc['key']['include'])

            for doc in include_history:
                if doc['key']['query_key'] in qobj:
                    qobj[doc['key']['query_key']].set_include_history(include, doc['key']['include'])

        for k, v in qobj.items():
            if v.is_match():
                match_dict = {
                    'diff_pct': v.diff_pct,
                    'diff_value': v.avg_now - v.avg_history,
                    'threshold_pct': self.threshold_pct,
                    self.qkey: k,
                    'agg_period': {},
                    'agg_history_period': {},
                }

                match_dict['agg_period'].update(v.include_now)
                match_dict['agg_history_period'].update(v.include_history)
                self.add_match(match_dict)

    def garbage_collect_bak(self, timestamp):
        epoch = self.get_epoch(timestamp)
        for k, v in self.qobj.items():
            elastalert_logger.info("Documents collected for %s so far: %s" % (k, str(v.diff_key_time)))

            diff_key_in_agg = []
            diff_key_in_agg_history = []

            agg_list = []
            for i in self.include:
                agg_list.append(set())

            agg_history_list = []
            for i in self.include:
                agg_history_list.append(set())

            agg_list_all = []
            for i in self.include_all:
                agg_list_all.append(list())

            agg_history_list_all = []
            for i in self.include_all:
                agg_history_list_all.append(list())

            # list of keys to deallocate in diff_key_time
            diff_key_del = []
            for key, value in v.diff_key_time.items():
                # if timestamp is inside aggregation period then store in diff_key_in_agg
                if epoch - value[0] <= self.agg_sec:
                    diff_key_in_agg.append(value[1])
                    for i, j in enumerate(v.include_dict[key]):
                        agg_list[i].add(j)
                    for i, j in enumerate(v.include_all_dict[key]):
                        agg_list_all[i].append(j)

                # if timestamp is inside aggregation period before now - delta_time then store in diff_key_in_agg_history
                if (epoch - value[0] <= self.delta_sec + self.agg_sec) and (epoch - value[0] >= self.delta_sec):
                    diff_key_in_agg_history.append(value[1])
                    for i, j in enumerate(v.include_dict[key]):
                        agg_history_list[i].add(j)
                    for i, j in enumerate(v.include_all_dict[key]):
                        agg_history_list_all[i].append(j)

                # if timestamp is before now - delta_time - aggregation period
                if epoch - value[0] > self.delta_sec + self.agg_sec:
                    # add to list for later deallocation
                    diff_key_del.append(key)

            elastalert_logger.info(
                "Values collected for %s in the present time window so far: %s" % (k, str(diff_key_in_agg)))
            elastalert_logger.info(
                "Values collected for %s in the past time window so far: %s" % (k, str(diff_key_in_agg_history)))
            for key in diff_key_del:
                # deallocate
                del v.diff_key_time[key]
                del v.include_dict[key]
                del v.include_all_dict[key]

            avg_now = 0
            avg_history = 0
            diff_pct = 0

            if len(diff_key_in_agg) > 0:
                avg_now = sum(diff_key_in_agg) / len(diff_key_in_agg)
            if len(diff_key_in_agg_history) > 0:
                avg_history = sum(diff_key_in_agg_history) / len(diff_key_in_agg_history)

            if avg_history == avg_now:
                diff_pct = 0
            elif avg_now == 0:
                diff_pct = 0
            elif avg_history == 0:
                diff_pct = 0
            elif avg_now < avg_history:
                diff_pct = (avg_history - avg_now) / avg_history * 100
            elif avg_now > avg_history:
                diff_pct = (avg_now - avg_history) / avg_now * 100

            is_match = False
            if abs(diff_pct) > self.threshold_pct:
                is_match = True

            if is_match == True:
                match_dict = {
                    'diff_pct': diff_pct,
                    'diff_value': avg_now - avg_history,
                    'threshold_pct': self.threshold_pct,
                    'agg_period': {},
                    'agg_history_period': {},
                }
                d = {}
                for i, j in enumerate(self.include):
                    d[j] = ",".join(agg_list[i])
                match_dict['agg_period'].update(d)

                d = {}
                for i, j in enumerate(self.include):
                    d[j] = ",".join(agg_history_list[i])
                match_dict['agg_history_period'].update(d)

                d = {}
                for i, j in enumerate(self.include_all):
                    d[j] = ",".join(agg_list_all[i])
                match_dict['agg_period'].update(d)

                d = {}
                for i, j in enumerate(self.include_all):
                    d[j] = ",".join(agg_history_list_all[i])
                match_dict['agg_history_period'].update(d)

                self.add_match(match_dict)

    def add_data(self, data):
        pass

    def add_data_bak(self, data):

        for d in data:
            try:
                if d[self.qkey] not in self.qobj:
                    self.qobj[d[self.qkey]] = self.Qkey(self)

                self.qobj[d[self.qkey]].set_entry(d)

            except KeyError:
                pass

    class Qkey:
        def __init__(self, outer_self):
            self.oself = outer_self
            # dict with doc id as key and list(timestamp, value of diff_key) as value
            self.diff_key_time = {}
            # docs with id as key and list of values from self.include keys as value
            self.include_dict = {}
            # docs with id as key and list of values from self.include_all keys as value
            self.include_all_dict = {}

            self.avg_now = 0
            self.avg_history = 0
            self.diff_pct = 0

            self.include_now = {}
            self.include_history = {}

        def set_entry(self, doc):
            try:
                value = doc
                for i in self.oself.diff_key:
                    value = value[i]
                epoch = self.oself.get_epoch(doc['@timestamp'])
                self.diff_key_time[doc['_id']] = [epoch, float(value)]
                self.include_dict[doc['_id']] = []
                self.include_all_dict[doc['_id']] = []

                for i in self.oself.include:
                    value = doc
                    i = i.split('.')
                    for x in i:
                        value = value[x]
                    self.include_dict[doc['_id']].append(str(value))

                for i in self.oself.include_all:
                    value = doc
                    i = i.split('.')
                    for x in i:
                        value = value[x]
                    self.include_all_dict[doc['_id']].append(str(value))
            except KeyError:
                raise KeyError

        def set_include_now(self, include_now_key, include_now_value):
            if include_now_key not in self.include_now:
                self.include_now[include_now_key] = []
            self.include_now[include_now_key].append(str(include_now_value))

        def set_include_history(self, include_history_key, include_history_value):
            if include_history_key not in self.include_history:
                self.include_history[include_history_key] = []
            self.include_history[include_history_key].append(str(include_history_value))

        def set_avg_now(self, avg_now):
            self.avg_now = avg_now

        def set_avg_history(self, avg_history):
            self.avg_history = avg_history

        def is_match(self):
            for k, v in self.include_now.items():
                self.include_now[k] = ','.join(v)

            for k, v in self.include_history.items():
                self.include_history[k] = ','.join(v)

            if self.avg_history == self.avg_now:
                self.diff_pct = 0
            elif self.avg_now == 0:
                self.diff_pct = 0
            elif self.avg_history == 0:
                self.diff_pct = 0
            elif self.avg_now < self.avg_history:
                self.diff_pct = (self.avg_history - self.avg_now) / self.avg_history * 100
            elif self.avg_now > self.avg_history:
                self.diff_pct = (self.avg_now - self.avg_history) / self.avg_now * 100

            if abs(self.diff_pct) > self.oself.threshold_pct:
                return True

            return False


var=$(curl -s --noproxy '*' -u logserver:logserver -XGET -H 'Content-Type: application/json' 'localhost:9200/test-scripted/_search?pretty&size=0' -d '{
"query": {
 "range": {
  "@timestamp": {
   "gte": "now-15m"
  }
 }
},
  "aggs": {
    "scripted_metric": {
        "scripted_metric": {
        "init_script": {
		"inline": "params._agg.epochNow = new Date().getTime(); params._agg.maxHash = new HashMap(); params._agg.call_duration = []"
	},
        "map_script": {
		"inline": "if(!doc[\"call_duration\"].empty && !doc[\"user\"].empty && !doc[\"@timestamp\"].empty){def hashValue = [0, \"\"]; params._agg.call_duration.add(doc[\"call_duration\"].value); hashValue[0] = doc[\"call_duration\"].value; hashValue[1] = doc[\"@timestamp\"].value; if(params._agg.maxHash.get(doc[\"user\"].value) != null){if(doc[\"call_duration\"].value > params._agg.maxHash.get(doc[\"user\"].value)[0]){params._agg.maxHash.put(doc[\"user\"].value, hashValue)}} else{params._agg.maxHash.put(doc[\"user\"].value, hashValue)}}"
	},
        "combine_script": {
		"inline": "long[] sum_count = new long[] {0, 0}; for(i in params._agg.call_duration){sum_count[0] += i} sum_count[1] = params._agg.call_duration.length; params._agg.maxHash.put(\"scripted_metric_epochNow\", params._agg.epochNow); params._agg.maxHash.put(\"scripted_metric_sum\", sum_count[0]); params._agg.maxHash.put(\"scripted_metric_count\", sum_count[1]); return params._agg.maxHash"
	},
	"reduce_script": {
		"inline": "long epochNow = 0; double avg = 0; long sum = 0; long count = 0; def below_avg_hash = new HashMap(); def maxHash = new HashMap(); for(i in params._aggs){epochNow = i.get(\"scripted_metric_epochNow\"); sum += i.get(\"scripted_metric_sum\"); count += i.get(\"scripted_metric_count\"); i.remove(\"scripted_metric_epochNow\"); i.remove(\"scripted_metric_sum\"); i.remove(\"scripted_metric_count\")} if(count != 0){avg = (double)sum/count} for(i in params._aggs){for(Map.Entry entry : i.entrySet()){if(maxHash.get(entry.getKey()) != null){if(entry.getValue()[0] > maxHash.get(entry.getKey())[0]){maxHash.put(entry.getKey(), entry.getValue())}} else{maxHash.put(entry.getKey(), entry.getValue())}}} for(Map.Entry entry : maxHash.entrySet()){if(entry.getValue()[0] < avg){below_avg_hash.put(entry.getKey(), entry.getValue())}} return [epochNow, 100-(double)below_avg_hash.size()/maxHash.size()*100]"
	}
      }
   }
  }
}' | jq '.aggregations.scripted_metric.value')

echo "$var"
timestamp=$(date -Iseconds -d @$(echo "$var" | jq '.[0]' | cut -c 1-10))
value=$(echo "$var" | jq '.[1]')
curl -s -u 'logserver:logserver' -H 'Content-Type: application/json' -XPOST 'http://localhost:9200/test-scripted/doc?pretty' -d "{\"@timestamp\": \"$timestamp\", \"above_avg_perc\": $value}"

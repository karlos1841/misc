using System;
using System.Collections.Generic;
using System.Linq;
using ChoETL;
using System.IO;
using Newtonsoft.Json;
using Elasticsearch.Net;
using FuzzySharp;
using FuzzySharp.PreProcess;
using Microsoft.VisualBasic.FileIO;
using System.Threading;
using System.Reflection;
using System.Text.RegularExpressions;
using java.util;
using edu.stanford.nlp.sequences;
using edu.stanford.nlp.ie.crf;
using OpenNLP.Tools.Tokenize;
using System.Xml;

namespace post
{
    static class Program
    {
        /// <summary>
        /// Główny punkt wejścia dla aplikacji.
        /// </summary>
        [STAThread]
        static void Main()
        {
            //Console.WriteLine(Fuzz.TokenSetRatio("beagh more", "beaghmore, carrigallen", PreprocessMode.Full));
            //Console.WriteLine(Fuzz.TokenSetRatio("carrigallen", "beaghmore, carrigallen"));
            //System.Environment.Exit(0);
            //Relative paths to project files

            //string user_search = "3 Kingston Terrace, Carrick.on.Shannon";
            //string user_search = "No 21 Acres Avenue, Acres Cove, Drumshanbo";
            //string user_search = "No.4 Cnoc Bofin, Dromod";
            //Console.WriteLine(Fuzz.PartialRatio("LEITRIM", "CO. LEITRIM"));

            Paths paths = new Paths();
            // init locality database
            DataBase locality = new DataBase(paths.path.localitiesPath, "county_id", "locality_id", "name");

            // init secondary locality database
            DataBase secLocality = new DataBase(paths.path.secLocalityPath, "county_id", "locality_id", "name");

            // init thorofare database
            DataBase thorofare = new DataBase(paths.path.thorofarePath, "county_id", "thorfare_id", "thorfare_name");

            // train data
            
            NERTrain train = new NERTrain(ref locality, ref secLocality, ref thorofare);
            try
            {
                File.OpenRead(paths.path.trainPath);
            }
            catch (IOException e)
            {
                Console.WriteLine("Generating training data...");
                train.createTrainingData(paths.path.fullAddressesPath, paths.path.trainPath);
                Console.WriteLine("Creating model...");
                train.createModelFromTrainingData(paths.path.trainPath, paths.path.modelPath, paths.path.properties);
            }

            //Console.WriteLine(Fuzz.Ratio("16", "16 MILL RACE, MANORHAMILTON, CO. LEITRIM, F91P9R6"));
            //Console.WriteLine(Fuzz.TokenSetRatio("", ""));
            //return;
            // use model
            /*
            NERAddress address = new NERAddress();
            train.useModel(paths.path.modelPath, "18 FINNAN APPTS, MARYMOUNT, CARRICK ON SHANNON", ref address);
            return;
            */

            foreach (string partial_file in Directory.EnumerateFiles(paths.path.partialAddresses, "*.csv"))
            {
                TextFieldParser parser = new TextFieldParser(partial_file);
                parser.TextFieldType = FieldType.Delimited;
                parser.SetDelimiters(",");
                string[] fields = parser.ReadFields();
                int address_index = 1;
                int county_index = 3;
                for (int field_index = 0; field_index < fields.Length; field_index++)
                {
                    if (string.Equals(fields[field_index], "address", StringComparison.OrdinalIgnoreCase))
                    {
                        address_index = field_index;
                    }
                    else if (string.Equals(fields[field_index], "county", StringComparison.OrdinalIgnoreCase))
                    {
                        county_index = field_index;
                    }
                }

                Stats statistics = new Stats();
                int doc_id = 0;
                while (!parser.EndOfData)
                {
                    doc_id += 1;
                    Console.Write("\n");
                    fields = parser.ReadFields();
                    string user_search = fields.GetValueAt<string>(address_index);
                    string county = fields.GetValueAt<string>(county_index);

                    // init elastic
                    Elastic obj = new Elastic(paths.path.indexPath);

                    var json = obj.ConvertCsvFileToJsonBulkList(partial_file);
                    // docs indexed starting from id 1
                    obj.SendJsonToElastic(json);

                    // query data
                    /*
                    string doc_id = "";
                    string user_search = "Rantoge, Aghacashel";
                    string top_match = obj.QueryElastic(user_search, ref doc_id);
                    Console.WriteLine(top_match);
                    */

                    // set processing flag in elastic
                    obj.updateDocument(doc_id.ToString(), @"{""doc"": {""flag"": ""processing""}}");
                    Console.WriteLine("Document in elasticsearch: " + obj.QueryElasticById(doc_id.ToString()));

                    // begin searching
                    Console.WriteLine("Searching...", Console.ForegroundColor = ConsoleColor.Green);
                    Console.ResetColor();

                    // init model
                    NERAddress address = new NERAddress();
                    // print what model found
                    string model_found = train.useModel(paths.path.modelPath, user_search, ref address);
                    Console.WriteLine("Model found: " + model_found);

                    // init NLP
                    NLP n = new NLP(user_search, county);
                    // find county_id from user_search
                    n.findCounty();

                    // find locality_ids from localities for county_id (may contain false positives because some secondary locality names are the same as locality names)
                    //n.findLocalities(ref locality);
                    //Console.WriteLine("locality ids: " + String.Join(", ", n.locality_ids));

                    // find secondary locality (optional, list may contain false positives)
                    //n.findSecLocalities(ref secLocality);
                    //Console.WriteLine("secondary locality ids: " + String.Join(", ", n.secLocality_ids));

                    // find thorofare ids
                    //n.findThorofare(ref thorofare);
                    //Console.WriteLine("thorofare ids: " + String.Join(", ", n.thorofare_ids));

                    // init FullAddress
                    FullAddress addr = new FullAddress(paths.path.fullAddressesPath, ref n, ref address);
                    // set flag to processed in elastic
                    obj.updateDocument(doc_id.ToString(), @"{""doc"": {""flag"": ""processed""}}");

                    // for maybe matches index nested objects
                    List<FullAddressFields> best_addr = addr.getBestAddresses(user_search, model_found);
                    if (best_addr.Count == 0)
                    {
                        Console.WriteLine("No matches found");
                        statistics.no_matches += 1;
                    }
                    else if (best_addr.Count == 1)
                    {
                        obj.updateDocument(doc_id.ToString(), @"{""doc"":" + JsonConvert.SerializeObject(best_addr[0]) + "}");
                        statistics.exact_matches += 1;
                    }
                    else
                    {
                        List<string> json_objects = new List<string>();
                        foreach (FullAddressFields best_addr_i in best_addr)
                        {
                            json_objects.Add(JsonConvert.SerializeObject(best_addr_i));
                        }

                        string nested_json = "[" + String.Join(",", json_objects) + "]";
                        obj.updateDocument(doc_id.ToString(), @"{""doc"": {""matches"": " + nested_json + "}}");
                        statistics.maybe_matches += 1;
                    }
                    Console.WriteLine("Document in elasticsearch: " + obj.QueryElasticById(doc_id.ToString()));

                    // Output only addresses
                    foreach (FullAddressFields best_addr_i in best_addr)
                    {
                        Console.WriteLine(best_addr_i.address, Console.ForegroundColor = ConsoleColor.Green);
                        Console.ResetColor();
                    }

                    //break;
                }

                // print statistics
                Console.WriteLine("Exact matches found: " + statistics.exact_matches);
                Console.WriteLine("One to many matches found: " + statistics.maybe_matches);
                Console.WriteLine("No matches found: " + statistics.no_matches);
            }
        }
    }

    class Stats
    {
        public long exact_matches;
        public long maybe_matches;
        public long no_matches;

        public Stats()
        {
            this.exact_matches = 0;
            this.maybe_matches = 0;
            this.no_matches = 0;
        }
    };

    struct CustomPaths
    {
        public string localitiesPath;
        public string secLocalityPath;
        public string thorofarePath;
        public string partialAddresses;
        public string fullAddressesPath;
        public string indexPath;
        public string trainPath;
        public string modelPath;
        public string properties;
    };

    class Paths
    {
        string cwd;
        public CustomPaths path;
        public Paths()
        {
            cwd = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
            path.localitiesPath = Path.Combine(cwd, @"localities");
            path.secLocalityPath = Path.Combine(cwd, @"secondary_localities");
            path.thorofarePath = Path.Combine(cwd, @"thorofares");
            path.fullAddressesPath = Path.Combine(cwd, @"FullAddresses_NEW.csv");
            path.partialAddresses = Path.Combine(cwd, @"partial_addresses");
            path.indexPath = Path.Combine(cwd, @"index.txt");
            path.trainPath = Path.Combine(cwd, @"training_data.txt");
            path.modelPath = Path.Combine(cwd, @"model_data.gz");
            path.properties = Path.Combine(cwd, @"properties.txt");


            foreach (var field in typeof(CustomPaths).GetFields(BindingFlags.Public))
            {
                try
                {
                    File.OpenRead(field.Name);
                }
                catch (IOException e)
                {
                    Console.WriteLine("Error reading " + field.Name);
                    System.Environment.Exit(-1);
                }
            }
        }
    }

    class Elastic
    {
        public string indexname;
        public string uri;

        ElasticLowLevelClient client;
        public Elastic(string index_path)
        {
            try
            {
                var line = File.ReadLines(index_path);
                indexname = line.First().Trim();
            }
            catch (IOException e)
            {
                Console.WriteLine("Error reading " + index_path);
                System.Environment.Exit(-1);
            }

            string user = "logserver";
            string password = "logserver";
            uri = "http://10.4.3.188:9200";
            var config = new ConnectionConfiguration(new Uri(uri)).BasicAuthentication(user, password);
            client = new ElasticLowLevelClient(config);

            Console.WriteLine("Connecting to ElasticSearch...");
            var response = client.Cat.Health<StringResponse>();
            if (response.HttpStatusCode != 200)
                System.Environment.Exit(-1);
            Console.WriteLine("OK", Console.ForegroundColor = ConsoleColor.Green);
            Console.ResetColor();
        }

        public List<string> ConvertCsvFileToJsonBulkList(string path)
        {
            var csv = new List<string[]>();
            var lines = File.ReadAllLines(path);

            foreach (string line in lines)
                csv.Add(line.Split(','));

            var properties = lines[0].Split(',');

            var json = new List<string>();
            var response = client.Indices.Get<StringResponse>(indexname);
            if (response.HttpStatusCode == 200)
            {
                Console.WriteLine("Index " + indexname + " already exists...");
                //Thread.Sleep(1000);
            }
            else
            {
                Thread.Sleep(1000);
                Console.WriteLine("Adding index " + indexname);
                for (int i = 1; i < lines.Length; i++)
                {
                    var objResult = new Dictionary<string, string>();
                    objResult.Add("message", lines[i]);
                    json.Add("{ \"index\" : { \"_index\" : \"" + indexname + "\", \"_id\": \"" + i + "\"} }");
                    json.Add(JsonConvert.SerializeObject(objResult));

                }
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("OK");
                Console.ResetColor();
                Thread.Sleep(1000);
            }
            return json;
        }

        public List<string> ConvertStringToJsonBulkList(string partial)
        {
            var objResult = new Dictionary<string, string>();
            objResult.Add("message", partial);
            var json = new List<string>();
            json.Add("{ \"index\" : { \"_index\" : \"" + indexname + "\" } }");
            json.Add(JsonConvert.SerializeObject(objResult));

            return json;
        }

        /*
         * dorobic SSL
         */
        public void SendJsonToElastic(List<string> json)
        {
            PostData body = PostData.MultiJson(json);
            var response = client.Bulk<BytesResponse>(indexname, body);
            //Debug.WriteLine(response.DebugInformation);


            foreach (string json_line in json)
            {
                Console.WriteLine(json_line);
            }
        }

        // todo: error checking
        public string QueryElastic(string token, ref string doc_id)
        {
            string query = "{\"query\": {\"match\": {\"message\": {\"query\": \"" + token +
                "\", \"fuzziness\": \"AUTO\"}}}}";
            PostData body = PostData.String(query);
            var response = client.Search<BytesResponse>(indexname, body);
            //Console.WriteLine(response.DebugInformation);

            dynamic obj = JsonConvert.DeserializeObject(System.Text.Encoding.UTF8.GetString(response.Body));
            string top_match = obj.hits.hits[0]._source.message;
            doc_id = obj.hits.hits[0]._id;
            return top_match;
        }

        public string QueryElasticById(string id)
        {
            var response = client.Get<BytesResponse>(indexname, id);
            dynamic obj = JsonConvert.DeserializeObject(System.Text.Encoding.UTF8.GetString(response.Body));
            return JsonConvert.SerializeObject(obj._source);
        }

        public void updateDocument(string id, string doc)
        {
            PostData body = PostData.String(doc);
            var response = client.Update<BytesResponse>(indexname, id, body);
        }
    }

    class DataBase
    {
        // county_id, county name
        public static Dictionary<int, string> counties = new Dictionary<int, string>{
                { 1, "CORK" },
                { 2, "KERRY" },
                { 3, "LIMERICK" },
                { 4, "CLARE"},
                { 5, "MAYO"},
                { 6, "GALWAY"},
                { 7, "LEITRIM"},
                { 8, "SLIGO"},
                { 9, "DONEGAL"},
                { 10, "WATERFORD"},
                { 11, "WEXFORD" },
                { 12, "WICKLOW" },
                { 13, "KILDARE"},
                { 14, "KILKENNY" },
                { 15, "TIPPERARY" },
                { 16, "OFFALY" },
                { 17, "LONGFORD"},
                { 18, "MONAGHAN" },
                { 19, "LOUTH"},
                { 20, "ROSCOMMON" },
                { 21, "MEATH" },
                { 22, "WESTMEATH" },
                { 23, "CARLOW" },
                { 24, "CAVAN" },
                { 25, "LAOIS" },
                { 26, "DUBLIN" },
                { 100, "UNKNOWN" },
                { 30, "ANTRIM" },
                { 31, "ARMAGH" },
                { 32, "DOWN" },
                { 33, "FERMANAGH" },
                { 34, "LONDONDERRY" },
                { 35, "TYRONE" }
            };

        // county_id, id, name
        public Dictionary<int, List<KeyValuePair<int, string>>> data = new Dictionary<int, List<KeyValuePair<int, string>>>();

        string dirpath;
        string county_column;
        string id_column;
        string name_column;

        public DataBase(string dirpath, string county_column, string id_column, string name_column)
        {
            this.dirpath = dirpath;
            this.county_column = county_column;
            this.id_column = id_column;
            this.name_column = name_column;
            initData();
        }
        private void initData()
        {
            // create keys based on county_id
            foreach (KeyValuePair<int, string> entry in counties)
            {
                data[entry.Key] = new List<KeyValuePair<int, string>>();
            }

            // read csv files and store data in dictionary
            foreach (string csv in Directory.EnumerateFiles(dirpath, "*.csv"))
            {
                var lines = File.ReadAllLines(csv);
                var header = lines[0].Split(',');
                for (int line_i = 1; line_i < lines.Length; line_i++)
                {
                    var cells = lines[line_i].Split(',');
                    // find county_id
                    for (int header_i = 0; header_i < header.Length; header_i++)
                    {
                        if (string.Equals(header[header_i], county_column, StringComparison.OrdinalIgnoreCase))
                        {
                            // add name
                            int id = -1;
                            string name = "";
                            for (int header_i2 = 0; header_i2 < header.Length; header_i2++)
                            {
                                if (string.Equals(header[header_i2], id_column, StringComparison.OrdinalIgnoreCase))
                                {
                                    id = int.Parse(cells[header_i2]);
                                }

                                if (string.Equals(header[header_i2], name_column, StringComparison.OrdinalIgnoreCase))
                                {
                                    name = cells[header_i2].ToLower();
                                }
                            }

                            // add pair for county_id
                            data[int.Parse(cells[header_i])].Add(new KeyValuePair<int, string>(id, name));
                        }
                    }
                }
            }

            /*
            foreach(KeyValuePair<int, List<KeyValuePair<int, string>>> entry in localities)
            {
                Debug.WriteLine(entry.Key);
                foreach(KeyValuePair<int, string> val in entry.Value)
                {
                    Debug.WriteLine(val.Key);
                    Debug.WriteLine(val.Value);
                }
            }
            */
        }
    }

    class FullAddressFields
    {
        public string locality;
        public string secondary_locality;
        public string thorofare;
        public string building_number;
        public string building_group_name;
        public string building_name;
        public string sub_building_name;
        public string department;
        public string organisation_name;

        public int county;
        public string address;
        public int building;
        public long address_reference;
    }

    class FullAddress
    {
        string addrPath;
        NLP nlp;
        NERAddress ner;
        string locality = "locality";
        string secLocality = "secondary_locality";
        string thorofare = "thoroughfare";
        string building_number = "building_number";
        string building_group_name = "building_group_name";
        string building_name = "building_name";
        string sub_building_name = "sub_building_name";
        string department = "department";
        string organisation_name = "organisation_name";

        string county = "county_id";
        string address = "address";
        string building = "building_id";
        string address_reference = "address_reference";
        int MIN_SCORE;

        public FullAddress(string addrPath, ref NLP nlp, ref NERAddress ner)
        {
            this.addrPath = addrPath;
            this.nlp = nlp;
            this.ner = ner;
            this.MIN_SCORE = 80;
        }
        public List<FullAddressFields> getBestAddresses(string user_search, string model_found)
        {
            TextFieldParser parser = new TextFieldParser(addrPath);
            parser.TextFieldType = FieldType.Delimited;
            parser.SetDelimiters(",");

            //Dictionary<int, string> header = new Dictionary<int, string>();
            string[] fields = parser.ReadFields();
            int fields_len = fields.Length;

            int county_id_index = -1;
            int locality_index = -1;
            int secLocality_index = -1;
            int thorofare_index = -1;
            int address_index = -1;
            int building_id_index = -1;
            int address_reference_index = -1;
            int building_number_index = -1;
            int building_group_name_index = -1;
            int building_name_index = -1;
            int sub_building_name_index = -1;
            int department_index = -1;
            int organisation_name_index = -1;
            for (int col_index = 0; col_index < fields_len; col_index++)
            {
                string value = fields.GetValueAt<string>(col_index);
                if (string.Equals(county, value, StringComparison.OrdinalIgnoreCase))
                    county_id_index = col_index;

                else if (string.Equals(locality, value, StringComparison.OrdinalIgnoreCase))
                    locality_index = col_index;

                else if (string.Equals(secLocality, value, StringComparison.OrdinalIgnoreCase))
                    secLocality_index = col_index;

                else if (string.Equals(thorofare, value, StringComparison.OrdinalIgnoreCase))
                    thorofare_index = col_index;

                else if (string.Equals(address, value, StringComparison.OrdinalIgnoreCase))
                    address_index = col_index;

                else if (string.Equals(building, value, StringComparison.OrdinalIgnoreCase))
                    building_id_index = col_index;

                else if (string.Equals(address_reference, value, StringComparison.OrdinalIgnoreCase))
                    address_reference_index = col_index;

                else if (string.Equals(building_number, value, StringComparison.OrdinalIgnoreCase))
                    building_number_index = col_index;

                else if (string.Equals(building_group_name, value, StringComparison.OrdinalIgnoreCase))
                    building_group_name_index = col_index;

                else if (string.Equals(building_name, value, StringComparison.OrdinalIgnoreCase))
                    building_name_index = col_index;

                else if (string.Equals(sub_building_name, value, StringComparison.OrdinalIgnoreCase))
                    sub_building_name_index = col_index;

                else if (string.Equals(department, value, StringComparison.OrdinalIgnoreCase))
                    department_index = col_index;

                else if (string.Equals(organisation_name, value, StringComparison.OrdinalIgnoreCase))
                    organisation_name_index = col_index;

                //header.Add(col_id, fields.GetValueAt<string>(col_id));
            }

            System.Collections.Generic.HashSet<FullAddressFields> top_addresses = new System.Collections.Generic.HashSet<FullAddressFields>();
            while (!parser.EndOfData)
            {
                fields = parser.ReadFields();

                // build json using full address fields
                FullAddressFields fulladdr = new FullAddressFields();
                fulladdr.locality = fields.GetValueAt<string>(locality_index);
                fulladdr.secondary_locality = fields.GetValueAt<string>(secLocality_index);
                fulladdr.thorofare = fields.GetValueAt<string>(thorofare_index);
                fulladdr.building_number = fields.GetValueAt<string>(building_number_index);
                fulladdr.county = fields.GetValueAt<int>(county_id_index);
                fulladdr.address = fields.GetValueAt<string>(address_index);
                fulladdr.building = fields.GetValueAt<int>(building_id_index);
                fulladdr.address_reference = fields.GetValueAt<long>(address_reference_index);

                fulladdr.building_group_name = fields.GetValueAt<string>(building_group_name_index);
                fulladdr.building_name = fields.GetValueAt<string>(building_name_index);
                fulladdr.sub_building_name = fields.GetValueAt<string>(sub_building_name_index);
                fulladdr.department = fields.GetValueAt<string>(department_index);
                fulladdr.organisation_name = fields.GetValueAt<string>(organisation_name_index);

                if (nlp.county_id == fulladdr.county)
                {
                    // narrow down the matches by using what model found
                    // get matches with > MIN_SCORE
                    string address_split = fulladdr.address.Replace(fulladdr.address.Split(",").Last(), "");
                    if (Fuzz.PartialTokenSetRatio(model_found, address_split.ToLower(), PreprocessMode.Full) > MIN_SCORE)
                    {
                        // narrow it down by checking similarity for locality, secondary locality and thorofare
                        if(!ner.locality.IsEmpty())
                        {
                            if (Fuzz.TokenSetRatio(ner.locality, fulladdr.locality.ToLower(), PreprocessMode.Full) < MIN_SCORE)
                                continue;
                        }
                        if(!ner.secondary_locality.IsEmpty())
                        {
                            if (Fuzz.TokenSetRatio(ner.secondary_locality, fulladdr.secondary_locality.ToLower(), PreprocessMode.Full) < MIN_SCORE)
                                continue;
                        }
                        if(!ner.thorofare.IsEmpty())
                        {
                            if (Fuzz.TokenSetRatio(ner.thorofare, fulladdr.thorofare.ToLower(), PreprocessMode.Full) < MIN_SCORE)
                                continue;
                        }
                        // if building number is present but not equal then go to next record
                        if(!ner.building_number.IsEmpty())
                        {
                            // not always building number is present in fulladdr.building_number
                            if (!fulladdr.building_number.IsEmpty())
                            {
                                if (ner.building_number != fulladdr.building_number.ToLower())
                                    continue;
                            }
                            else if (!fulladdr.sub_building_name.IsEmpty())
                            {
                                if (ner.building_number != fulladdr.sub_building_name.ToLower())
                                    continue;
                            }
                            else if (!fulladdr.building_name.IsEmpty())
                            {
                                if (ner.building_number != fulladdr.building_name.ToLower())
                                    continue;
                            }
                            else if (!fulladdr.building_group_name.IsEmpty())
                            {
                                if (ner.building_number != fulladdr.building_group_name.ToLower())
                                    continue;
                            }
                            else
                                continue;
                        }

                        top_addresses.Add(fulladdr);
                    }
                }
                //break;
            }
            // return empty if no matches found
            if (top_addresses.Count == 0)
                return new List<FullAddressFields>();

            // last processing, match user search against full address to get rid of similar wrong matches
            Dictionary<int, List<FullAddressFields>> score_address = new Dictionary<int, List<FullAddressFields>>();
            foreach (FullAddressFields fulladdr in top_addresses)
            {
                string address_split = fulladdr.address.Replace(fulladdr.address.Split(",").Last(), "");
                Console.WriteLine(address_split);
                int score = Fuzz.WeightedRatio(user_search, address_split.ToLower(), PreprocessMode.Full);
                Console.WriteLine("Score: " + score);
                if (!score_address.ContainsKey(score))
                    score_address[score] = new List<FullAddressFields>();

                //Console.WriteLine(addr);
                //Console.WriteLine(score);
                //Console.WriteLine(addr_parsed);
                score_address[score].Add(fulladdr);
            }

            int max_score = score_address.Keys.Max();
            List<FullAddressFields> score_addresses;
            score_address.TryGetValue(max_score, out score_addresses);
            return score_addresses;
        }
    }

    class NLP
    {
        public string partialAddress;
        public string county;
        public int county_id = -1;
        public List<int> locality_ids = new List<int>();
        public List<int> secLocality_ids = new List<int>();
        public List<int> thorofare_ids = new List<int>();

        public NLP(string user_search, string county)
        {
            partialAddress = user_search.ToLower();
            this.county = county;
            this.Tokenize();
        }

        public void findCounty()
        {
            foreach (KeyValuePair<int, string> entry in DataBase.counties)
            {
                if (string.Equals(county, entry.Value, StringComparison.OrdinalIgnoreCase))
                {
                    county_id = entry.Key;
                    return;
                }
            }
        }

        private List<int> fuzzyFind(ref DataBase d)
        {
            List<KeyValuePair<int, int>> locality_id_score = new List<KeyValuePair<int, int>>();

            foreach (KeyValuePair<int, List<KeyValuePair<int, string>>> entry in d.data)
            {
                if (entry.Key == county_id)
                {
                    foreach (KeyValuePair<int, string> loc in entry.Value)
                    {
                        // fuzzy matching based on tokenizing without ordering using full process
                        locality_id_score.Add(new KeyValuePair<int, int>(loc.Key, Fuzz.TokenSetRatio(Regex.Replace(loc.Value, @"\s+", String.Empty), Regex.Replace(partialAddress, @"\s+", String.Empty), PreprocessMode.Full)));
                    }
                }
            }
            if (locality_id_score.Count == 0)
                return new List<int>();

            List<int> top_locality_ids = new List<int>();
            int max_score = locality_id_score.Max(x => x.Value);

            // find ids with best score
            foreach (KeyValuePair<int, int> entry in locality_id_score)
            {
                if (entry.Value == max_score)
                {
                    top_locality_ids.Add(entry.Key);
                }
            }

            return top_locality_ids;
        }

        public void findLocalities(ref DataBase d)
        {
            locality_ids = fuzzyFind(ref d);
        }

        public void findSecLocalities(ref DataBase d)
        {
            // secondary locality is optional so the list may contain false positives
            secLocality_ids = fuzzyFind(ref d);
        }
        public void findThorofare(ref DataBase d)
        {
            thorofare_ids = fuzzyFind(ref d);
        }
        private void Tokenize()
        {
            //var t = new EnglishMaximumEntropyTokenizer(modelPath);
            //var t = new EnglishRuleBasedTokenizer(false);
        }
    }

    class NERAddress
    {
        public string locality;
        public string secondary_locality;
        public string thorofare;
        public string building_group_name;
        public string building_name;
        public string sub_building_name;
        public string building_number;
        public string department;
        public string organisation_name;

        public NERAddress()
        {
            this.locality = "";
            this.secondary_locality = "";
            this.thorofare = "";
            this.building_group_name = "";
            this.building_name = "";
            this.sub_building_name = "";
            this.building_number = "";
            this.department = "";
            this.organisation_name = "";
        }
    }

    class NERTrain
    {
        int score_similarity;
        Dictionary<string, string> data_to_train;
        DataBase db_locality;
        DataBase db_secondary_locality;
        DataBase db_thorofare;
        public NERTrain(ref DataBase locality, ref DataBase secondary_locality, ref DataBase thorofare)
        {
            this.db_locality = locality;
            this.db_secondary_locality = secondary_locality;
            this.db_thorofare = thorofare;
        }

        public void createTrainingData(string inputPath, string outputPath)
        {
            StreamWriter f = File.CreateText(outputPath);

            TextFieldParser parser = new TextFieldParser(inputPath);
            parser.TextFieldType = FieldType.Delimited;
            parser.SetDelimiters(",");

            string[] fields = parser.ReadFields();

            int address = 0;

            int organisation_name = 0;
            int department = 0;
            int building_number = 0;
            int sub_building_name = 0;
            int building_name = 0;
            int building_group_name = 0;
            int thorofare = 0;
            int locality = 0;
            int secondary_locality = 0;
            int county = 0;
            for (int index = 0; index < fields.Length; index++)
            {
                string value = fields.GetValueAt<string>(index);
                if (string.Equals("address", value, StringComparison.OrdinalIgnoreCase))
                    address = index;

                else if (string.Equals("organisation_name", value, StringComparison.OrdinalIgnoreCase))
                    organisation_name = index;

                else if (string.Equals("department", value, StringComparison.OrdinalIgnoreCase))
                    department = index;

                else if (string.Equals("building_number", value, StringComparison.OrdinalIgnoreCase))
                    building_number = index;

                else if (string.Equals("sub_building_name", value, StringComparison.OrdinalIgnoreCase))
                    sub_building_name = index;

                else if (string.Equals("building_name", value, StringComparison.OrdinalIgnoreCase))
                    building_name = index;

                else if (string.Equals("building_group_name", value, StringComparison.OrdinalIgnoreCase))
                    building_group_name = index;

                else if (string.Equals("thoroughfare", value, StringComparison.OrdinalIgnoreCase))
                    thorofare = index;

                else if (string.Equals("locality", value, StringComparison.OrdinalIgnoreCase))
                    locality = index;

                else if (string.Equals("secondary_locality", value, StringComparison.OrdinalIgnoreCase))
                    secondary_locality = index;

                else if (string.Equals("county", value, StringComparison.OrdinalIgnoreCase))
                    county = index;

            }

            while (!parser.EndOfData)
            {
                fields = parser.ReadFields();

                // get value in columns
                //int county_value = fields.GetValueAt<int>(county_id);
                //int locality_value = fields.GetValueAt<int>(locality_id);
                //int secondary_locality_value = fields.GetValueAt<int>(secondary_locality_id);
                //int thorofare_value = fields.GetValueAt<int>(thorofare_id);

                // find county
                //string county_str = findCounty(county_value);

                // find locality
                //string locality_str = findLocality(county_value, locality_value);

                // find secondary locality
                //string secondary_locality_str = findSecondaryLocality(county_value, secondary_locality_value);

                // find thorfare
                //string thorofare_str = findThorofare(county_value, thorofare_value);

                // get value in columns
                string county_value = fields.GetValueAt<string>(county).ToLower();
                string locality_value = fields.GetValueAt<string>(locality).ToLower();
                string secondary_locality_value = fields.GetValueAt<string>(secondary_locality).ToLower();
                string thorofare_value = fields.GetValueAt<string>(thorofare).ToLower();

                string building_group_name_value = fields.GetValueAt<string>(building_group_name).ToLower();
                string building_name_value = fields.GetValueAt<string>(building_name).ToLower();
                string sub_building_name_value = fields.GetValueAt<string>(sub_building_name).ToLower();

                string building_number_value = fields.GetValueAt<string>(building_number).ToLower();

                string department_value = fields.GetValueAt<string>(department).ToLower();
                string organisation_name_value = fields.GetValueAt<string>(organisation_name).ToLower();

                // make address lowercase
                string address_str = fields.GetValueAt<string>(address).ToLower();

                // tokenize address
                var tokenizer = new EnglishRuleBasedTokenizer(false);
                string[] tokenized_address = tokenizer.Tokenize(address_str);

                List<KeyValuePair<string, string>> token_tag_list = new List<KeyValuePair<string, string>>();


                for(int index = 0; index < tokenized_address.Length; index++)
                {
                    bool found = false;

                    int index2 = index;
                    Dictionary<string, List<int>> chunk_scores = new Dictionary<string, List<int>>();
                    while(index2 < tokenized_address.Length && tokenized_address[index2] != ",")
                    {
                        if (!chunk_scores.ContainsKey("COUNTY"))
                            chunk_scores["COUNTY"] = new List<int>();

                        if (!chunk_scores.ContainsKey("LOCALITY"))
                            chunk_scores["LOCALITY"] = new List<int>();

                        if (!chunk_scores.ContainsKey("SECONDARY_LOCALITY"))
                            chunk_scores["SECONDARY_LOCALITY"] = new List<int>();

                        if (!chunk_scores.ContainsKey("THOROFARE"))
                            chunk_scores["THOROFARE"] = new List<int>();

                        if (!chunk_scores.ContainsKey("BUILDING_GROUP_NAME"))
                            chunk_scores["BUILDING_GROUP_NAME"] = new List<int>();

                        if (!chunk_scores.ContainsKey("BUILDING_NAME"))
                            chunk_scores["BUILDING_NAME"] = new List<int>();

                        if (!chunk_scores.ContainsKey("SUB_BUILDING_NAME"))
                            chunk_scores["SUB_BUILDING_NAME"] = new List<int>();

                        if (!chunk_scores.ContainsKey("BUILDING_NUMBER"))
                            chunk_scores["BUILDING_NUMBER"] = new List<int>();

                        if (!chunk_scores.ContainsKey("DEPARTMENT"))
                            chunk_scores["DEPARTMENT"] = new List<int>();

                        if (!chunk_scores.ContainsKey("ORGANISATION_NAME"))
                            chunk_scores["ORGANISATION_NAME"] = new List<int>();


                            chunk_scores["COUNTY"].Add(Fuzz.TokenSetRatio(tokenized_address[index2], county_value));
                            chunk_scores["LOCALITY"].Add(Fuzz.TokenSetRatio(tokenized_address[index2], locality_value));
                            chunk_scores["SECONDARY_LOCALITY"].Add(Fuzz.TokenSetRatio(tokenized_address[index2], secondary_locality_value));
                            chunk_scores["THOROFARE"].Add(Fuzz.TokenSetRatio(tokenized_address[index2], thorofare_value));
                            chunk_scores["BUILDING_GROUP_NAME"].Add(Fuzz.TokenSetRatio(tokenized_address[index2], building_group_name_value));
                            chunk_scores["BUILDING_NAME"].Add(Fuzz.TokenSetRatio(tokenized_address[index2], building_name_value));
                            chunk_scores["SUB_BUILDING_NAME"].Add(Fuzz.TokenSetRatio(tokenized_address[index2], sub_building_name_value));
                            chunk_scores["BUILDING_NUMBER"].Add(Fuzz.TokenSetRatio(tokenized_address[index2], building_number_value));
                            chunk_scores["DEPARTMENT"].Add(Fuzz.TokenSetRatio(tokenized_address[index2], department_value));
                            chunk_scores["ORGANISATION_NAME"].Add(Fuzz.TokenSetRatio(tokenized_address[index2], organisation_name_value));

                            index2 += 1;
                        }

                        foreach (KeyValuePair<string, List<int>> chunk_score in chunk_scores)
                        {
                            // check if chunk upto "," belongs to one entity
                            if (chunk_score.Value.Min() == 100)
                            {
                            if (!token_tag_list.Contains(new KeyValuePair<string, string>(tokenized_address[index], chunk_score.Key)))
                            {
                                token_tag_list.Add(new KeyValuePair<string, string>(tokenized_address[index], chunk_score.Key));
                                found = true;
                                break;
                            }
                            }
                            // building numbers are part of another entity
                            else if(chunk_score.Key == "BUILDING_NUMBER" && chunk_score.Value.Max() == 100)
                            {
                                token_tag_list.Add(new KeyValuePair<string, string>(building_number_value, chunk_score.Key));
                                found = true;
                            }
                        }

                    // if not found then assign 0
                    if (!found)
                    {
                        token_tag_list.Add(new KeyValuePair<string, string>(tokenized_address[index], "0"));
                    }
                }

                foreach (KeyValuePair<string, string> token_tag in token_tag_list)
                {
                    //Console.Write(token_tag.Key);
                    //Console.Write("\t");
                    //Console.WriteLine(token_tag.Value);

                    // write to file
                    f.Write(token_tag.Key);
                    f.Write("\t");
                    f.WriteLine(token_tag.Value);
                }
                f.WriteLine();
                //break;
            }

            f.Close();
        }

        public void createModelFromTrainingData(string inputPath, string outputPath, string properties)
        {
            Properties props = edu.stanford.nlp.util.StringUtils.propFileToProperties(properties);
            props.setProperty("serializeTo", outputPath);

            if (inputPath != null)
                props.setProperty("trainFile", inputPath);

            SeqClassifierFlags flags = new SeqClassifierFlags(props);
            CRFClassifier crf = new CRFClassifier(flags);

            crf.train();
            crf.serializeClassifier(outputPath);
        }

        public string useModel(string inputPath, string partial_address, ref NERAddress addr)
        {
            // training data uses lowercase
            partial_address = partial_address.ToLower();

            CRFClassifier model = CRFClassifier.getClassifierNoExceptions(inputPath);

            //string tagged_address = model.classifyToString(partial_address);
            string tagged_address = model.classifyWithInlineXML(partial_address);
            tagged_address = tagged_address.Replace("<0>", "<ZERO>");
            tagged_address = tagged_address.Replace("</0>", "</ZERO>");

            // parse xml
            XmlDocument doc = new XmlDocument();
            doc.LoadXml("<root>" + tagged_address + "</root>");

            Console.WriteLine("Model output: " + tagged_address);

            foreach (XmlNode node in doc.DocumentElement.ChildNodes)
            {
                if (node.Name ==  "LOCALITY")
                    addr.locality += node.InnerText + " ";

                if (node.Name == "SECONDARY_LOCALITY")
                    addr.secondary_locality += node.InnerText + " ";

                if (node.Name == "THOROFARE")
                    addr.thorofare += node.InnerText + " ";

                if (node.Name == "BUILDING_GROUP_NAME")
                    addr.building_group_name += node.InnerText + " ";

                if (node.Name == "BUILDING_NAME")
                    addr.building_name += node.InnerText + " ";

                if (node.Name == "SUB_BUILDING_NAME")
                    addr.sub_building_name += node.InnerText + " ";

                if (node.Name == "BUILDING_NUMBER")
                    addr.building_number += node.InnerText + " ";

                if (node.Name == "DEPARTMENT")
                    addr.department += node.InnerText + " ";

                if (node.Name == "ORGANISATION_NAME")
                    addr.organisation_name += node.InnerText + " ";
            }

            string response = addr.organisation_name + addr.department + addr.building_number +
                    addr.sub_building_name + addr.building_name + addr.building_group_name +
                    addr.thorofare + addr.secondary_locality + addr.locality;

            addr.locality = addr.locality.Trim();
            addr.secondary_locality = addr.secondary_locality.Trim();
            addr.thorofare = addr.thorofare.Trim();
            addr.building_group_name = addr.building_group_name.Trim();
            addr.building_name = addr.building_name.Trim();
            addr.sub_building_name = addr.sub_building_name.Trim();
            addr.building_number = addr.building_number.Trim();
            addr.department = addr.department.Trim();
            addr.organisation_name = addr.organisation_name.Trim();

            return response;
        }

        string findCounty(int county)
        {
            foreach (KeyValuePair<int, string> entry in DataBase.counties)
            {
                if (county == entry.Key)
                {
                    return entry.Value;
                }
            }

            return "";
        }

        string findLocality(int county, int locality)
        {
            foreach (KeyValuePair<int, List<KeyValuePair<int, string>>> entry in db_locality.data)
            {
                if (county == entry.Key)
                {
                    foreach (KeyValuePair<int, string> locality_pair in entry.Value)
                    {
                        if (locality == locality_pair.Key)
                        {
                            return locality_pair.Value;
                        }
                    }
                }
            }

            return "";
        }

        string findSecondaryLocality(int county, int secondary_locality)
        {
            foreach (KeyValuePair<int, List<KeyValuePair<int, string>>> entry in db_secondary_locality.data)
            {
                if (county == entry.Key)
                {
                    foreach (KeyValuePair<int, string> secondary_locality_pair in entry.Value)
                    {
                        if (secondary_locality == secondary_locality_pair.Key)
                        {
                            return secondary_locality_pair.Value;
                        }
                    }
                }
            }

            return "";
        }

        string findThorofare(int county, int thorofare)
        {
            foreach (KeyValuePair<int, List<KeyValuePair<int, string>>> entry in db_thorofare.data)
            {
                if (county == entry.Key)
                {
                    foreach (KeyValuePair<int, string> thorofare_pair in entry.Value)
                    {
                        if (thorofare == thorofare_pair.Key)
                        {
                            return thorofare_pair.Value;
                        }
                    }
                }
            }

            return "";
        }
    }
}
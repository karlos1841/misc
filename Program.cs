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
using System.Collections;

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
            /*
            var t = new F23.StringSimilarity.Damerau();
            Console.WriteLine(t.Distance("", "12windmillpark"));
            Console.WriteLine(Fuzz.PartialRatio("woodgreen", "wood"));
            return;
            */
            // use model
            /*
            //string user_search_test = "6Woodlands Avenue, Dromahair";
            string user_search_test = "";
            string county_test = "leitrim";
            NERAddress address_test = new NERAddress();
            string[] user_search_test_normalized = address_test.normalize(user_search_test);

            // print input address
            Console.WriteLine("Input address: " + user_search_test);
            // print normalized address
            Console.WriteLine("Normalized address[tokens]: " + string.Join("|", user_search_test_normalized));
            train.useModel(paths.path.modelPath, user_search_test.ToLower(), ref address_test);

            FullAddress addr_test = new FullAddress(paths.path.fullAddressesPath, ref address_test);
            List<FullAddressFields> best_addr_test = addr_test.getBestAddresses(county_test, user_search_test_normalized);
            foreach (FullAddressFields best_addr_i in best_addr_test)
            {
                Console.WriteLine(best_addr_i.address, Console.ForegroundColor = ConsoleColor.Green);
                Console.ResetColor();
            }

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

                    // print input address
                    Console.WriteLine("Input address: " + user_search);

                    // set processing flag in elastic
                    obj.updateDocument(doc_id.ToString(), @"{""doc"": {""flag"": ""processing""}}");
                    Console.WriteLine("Document in elasticsearch: " + obj.QueryElasticById(doc_id.ToString()));

                    // begin searching
                    Console.WriteLine("Searching...", Console.ForegroundColor = ConsoleColor.Green);
                    Console.ResetColor();

                    // init model
                    NERAddress address = new NERAddress();
                    // normalize data
                    string[] user_search_normalized = address.normalize(user_search);

                    // print what model found
                    train.useModel(paths.path.modelPath, user_search.ToLower(), ref address);

                    // print normalized address
                    Console.WriteLine("Normalized address[tokens]: " + string.Join("|", user_search_normalized));

                    // init FullAddress
                    FullAddress addr = new FullAddress(paths.path.fullAddressesPath, ref address);
                    // set flag to processed in elastic
                    obj.updateDocument(doc_id.ToString(), @"{""doc"": {""flag"": ""processed""}}");

                    // for maybe matches index nested objects
                    List<FullAddressFields> best_addr = addr.getBestAddresses(county, user_search_normalized);
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

        public string county;
        public string address;
        public int building;
        public long address_reference;
    }

    class FullAddress
    {
        string addrPath;
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

        string county = "county";
        string address = "address";
        string building = "building_id";
        string address_reference = "address_reference";
        double MIN_SCORE;
        double MAX_DIST;

        public FullAddress(string addrPath, ref NERAddress ner)
        {
            this.addrPath = addrPath;
            this.ner = ner;
            this.MIN_SCORE = 0.90;
            this.MAX_DIST = 2;
        }
        public List<FullAddressFields> getBestAddresses(string user_county, string[] user_search_normalized)
        {
            TextFieldParser parser = new TextFieldParser(addrPath);
            parser.TextFieldType = FieldType.Delimited;
            parser.SetDelimiters(",");

            //Dictionary<int, string> header = new Dictionary<int, string>();
            string[] fields = parser.ReadFields();
            int fields_len = fields.Length;

            int county_index = -1;
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
                    county_index = col_index;

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
            List<int> address_scores = new List<int>();
            while (!parser.EndOfData)
            {
                fields = parser.ReadFields();

                // build json using full address fields
                FullAddressFields fulladdr = new FullAddressFields();
                fulladdr.locality = fields.GetValueAt<string>(locality_index);
                fulladdr.secondary_locality = fields.GetValueAt<string>(secLocality_index);
                fulladdr.thorofare = fields.GetValueAt<string>(thorofare_index);
                fulladdr.building_number = fields.GetValueAt<string>(building_number_index);
                fulladdr.county = fields.GetValueAt<string>(county_index);
                fulladdr.address = fields.GetValueAt<string>(address_index);
                fulladdr.building = fields.GetValueAt<int>(building_id_index);
                fulladdr.address_reference = fields.GetValueAt<long>(address_reference_index);

                fulladdr.building_group_name = fields.GetValueAt<string>(building_group_name_index);
                fulladdr.building_name = fields.GetValueAt<string>(building_name_index);
                fulladdr.sub_building_name = fields.GetValueAt<string>(sub_building_name_index);
                fulladdr.department = fields.GetValueAt<string>(department_index);
                fulladdr.organisation_name = fields.GetValueAt<string>(organisation_name_index);

                if (string.Equals(user_county, fulladdr.county, StringComparison.OrdinalIgnoreCase))
                {
                    string address_split = fulladdr.address.Replace(fulladdr.address.Split(",").Last(), "");
                    string[] address_split_normalized = ner.normalize(address_split);

                    if (!fulladdr.building_number.IsEmpty())
                    {
                        if (!ner.numbers.Contains<string>(fulladdr.building_number.ToLower().Trim()))
                            continue;
                    }
                    if (!fulladdr.sub_building_name.IsEmpty())
                    {
                        string sub_building_number = Regex.Replace(fulladdr.sub_building_name, @"(apartment)|(unit)|(flat)", "", RegexOptions.IgnoreCase).ToLower().Trim();
                        if (!ner.numbers.Contains<string>(sub_building_number))
                            continue;
                    }


                    int tokens_match = 0;
                    var sim = new F23.StringSimilarity.JaroWinkler();
                    var dist = new F23.StringSimilarity.Damerau();
                    foreach (string user_search_token in user_search_normalized)
                    {
                        foreach (string address_token in address_split_normalized)
                        {
                            //double score = sim.Similarity(user_search_token, address_token);
                            double score = Fuzz.WeightedRatio(user_search_token, address_token, PreprocessMode.Full);
                            double score2 = dist.Distance(user_search_token, address_token);
                            if(score >= MIN_SCORE * 100 || score2 <= MAX_DIST)
                            //if (score >= MIN_SCORE && score2 <= MAX_DIST)
                            {
                                tokens_match += 1;
                                break;
                            }
                        }
                    }

                    if (user_search_normalized.Length == tokens_match)
                    {
                        top_addresses.Add(fulladdr);
                    }

                    //break;
                }
            }

            List<FullAddressFields> best_addresses = new List<FullAddressFields>();
            foreach(FullAddressFields addr in top_addresses)
            {
                if(!addr.building_number.IsEmpty() || !addr.sub_building_name.IsEmpty())
                {
                    best_addresses.Add(addr);
                }
            }

            if (best_addresses.Count == 0)
                return top_addresses.ToList<FullAddressFields>();

            return best_addresses;
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

        public string[] numbers;

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

        public string[] normalize(string user_search)
        {
            // user_search
            user_search = user_search.ToLower();

            // try to normalize address, removing words and trimming in the first part
            string[] keywords = { "(no\\.)", "(no:)", "(no )" };
            string first_part = user_search.Split(",")[0];
            var first_match = Regex.Matches(first_part, string.Join("|", keywords), RegexOptions.IgnoreCase);
            if (first_match.Count > 0)
            {
                foreach (var keyword_match in first_match)
                {
                    var regex = new Regex(keyword_match.ToString());
                    string first_part_replace = regex.Replace(first_part, "", 1);
                    user_search = user_search.Replace(first_part, first_part_replace);
                    first_part = first_part_replace;
                }
            }
            user_search = user_search.Trim();

            // replace words in whole search
            Dictionary<string, string> replace_keywords = new Dictionary<string, string>();
            replace_keywords.Add("apt ", "apartment ");
            replace_keywords.Add("apt.", "apartment ");
            replace_keywords.Add("saint", "st");
            replace_keywords.Add("co.", "");

            foreach (KeyValuePair<string, string> replace_keyword in replace_keywords)
            {
                if (Regex.IsMatch(user_search, replace_keyword.Key, RegexOptions.IgnoreCase))
                {
                    user_search = user_search.Replace(replace_keyword.Key, replace_keyword.Value);
                }
            }

            // tokenize
            var user_search_tokenized = user_search.Split(new string[] { "," }, StringSplitOptions.RemoveEmptyEntries);
            user_search_tokenized = string.Join("", user_search_tokenized).Split(new string[] { " " }, StringSplitOptions.RemoveEmptyEntries);
            // remove spaces, dots, etc.
            for(int i = 0; i < user_search_tokenized.Length; i++)
            {
                user_search_tokenized[i] = user_search_tokenized[i].Replace(".", "").Replace("-", "").Replace("'", "").Replace("\"", "").Replace(":", "");
            }

            return user_search_tokenized;
        }
    }

    class NERTrain
    {
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
                string[] values = {
                    fields.GetValueAt<string>(county).ToLower(),
                    fields.GetValueAt<string>(locality).ToLower(),
                    fields.GetValueAt<string>(secondary_locality).ToLower(),
                    fields.GetValueAt<string>(thorofare).ToLower(),
                    fields.GetValueAt<string>(building_group_name).ToLower(),
                    fields.GetValueAt<string>(building_name).ToLower(),
                    fields.GetValueAt<string>(sub_building_name).ToLower(),
                    fields.GetValueAt<string>(building_number).ToLower(),
                    fields.GetValueAt<string>(department).ToLower(),
                    fields.GetValueAt<string>(organisation_name).ToLower()
            };

                // tokenize column values and assign IOB tags
                Dictionary<string, string[]> tokens = new Dictionary<string, string[]>();
                var tokenizer = new EnglishRuleBasedTokenizer(false);

                string[] tokenized_organisation_name = tokenizer.Tokenize(values[9]);
                tokens["ORGANISATION_NAME"] = tokenized_organisation_name;

                string[] tokenized_department = tokenizer.Tokenize(values[8]);
                tokens["DEPARTMENT"] = tokenized_department;

                string[] tokenized_building_number = tokenizer.Tokenize(values[7]);
                tokens["BUILDING_NUMBER"] = tokenized_building_number;

                string[] tokenized_sub_building_name = tokenizer.Tokenize(values[6]);
                tokens["SUB_BUILDING_NAME"] = tokenized_sub_building_name;

                string[] tokenized_building_name = tokenizer.Tokenize(values[5]);
                tokens["BUILDING_NAME"] = tokenized_building_name;

                string[] tokenized_building_group_name = tokenizer.Tokenize(values[4]);
                tokens["BUILDING_GROUP_NAME"] = tokenized_building_group_name;

                string[] tokenized_thorofare = tokenizer.Tokenize(values[3]);
                tokens["THOROFARE"] = tokenized_thorofare;

                string[] tokenized_secondary_locality = tokenizer.Tokenize(values[2]);
                tokens["SECONDARY_LOCALITY"] = tokenized_secondary_locality;

                string[] tokenized_locality = tokenizer.Tokenize(values[1]);
                tokens["LOCALITY"] = tokenized_locality;

                string[] tokenized_county = tokenizer.Tokenize(values[0]);
                tokens["COUNTY"] = tokenized_county;

                // generate variations of data for training
                // EDIT: tag only numbers for training
                //Dictionary<string[], string[]> tagged_tokens = addIOB(ref tokens);

                // manually crafted data to teach about building numbers/apartments/units, etc.
                string[] training_data = {
                    "no.\t0\n4\tNUMBER\ncnoc\t0\nbofin\t0\n,\t0\ndromod\t0\n",
                    "10\tNUMBER\ncroi\t0\nna\t0\ncarraige\t0\n,\t0\nkeshcarrigan\t0\n",
                    "high\t0\nstreet\t0\n,\t0\nballinamore\t0\n",
                    "no\t0\n4\tNUMBER\ncluain\t0\nard\t0\n,\t0\ncarrick-on-shannon\t0\n",
                    "no\t0\n1\tNUMBER\n.\t0\nst\t0\n.\t0\nciallian\t0\n's\t0\nview\t0\n,\t0\nfenagh\t0\n",
                    "no.\t0\n12\tNUMBER\n,\t0\nwindmill\t0\npark\t0\n,\t0\ndrumkeerin\t0\n",
                    "24\tNUMBER\nhillcrest\t0\n,\t0\nstonebridge\t0\n,\t0\ndromahaire\t0\n",
                    "apartment\t0\nno.\t0\n11\tNUMBER\nallen\t0\napartments\t0\n,\t0\nshannon\t0\ncourt\t0\n,\t0\ncarrick-on-shannon\t0\n",
                    "apt\t0\n17\tNUMBER\nthe\t0\nplaza\t0\n,\t0\ncentral\t0\npark\t0\n,\t0\ncarrick-on-shannon\t0\n",
                    "apartment\t0\nno\t0\n6\tNUMBER\nsummerhaven\t0\n,\t0\nsummerhill\t0\n,\t0\ncarrick-on-shannon\t0\n",
                    "unit\t0\n11\tNUMBER\n,\t0\nbridge\t0\nlane\t0\n,\t0\ncarrick\t0\non\t0\nshannon\t0\n",
                    "apartment\t0\n6\tNUMBER\nboderg\t0\n,\t0\nshannon\t0\ncourt\t0\n,\t0\ncarrick-on-shannon\t0\n",
                    "40a\tNUMBER\ninver\t0\ngael\t0\n,\t0\ncortober\t0\n,\t0\ncarrick\t0\non\t0\nshannon\t0\n",
                    "no:\t0\n73\tNUMBER\nmelvin\t0\nfields\t0\n,\t0\nkinlough\t0\n,\t0\nco.\t0\nleitrim\t0\n",
                    "32b\tNUMBER\nhillcrest\t0\ngrove\t0\n,\t0\ndrumshambo\t0\n",
                    "15\tNUMBER\n29\tNUMBER\n32\tNUMBER\n33\tNUMBER\n34\tNUMBER\n35\tNUMBER\n38\tNUMBER\n53\tNUMBER\n54\tNUMBER\n57\tNUMBER\n,\t0\nliscara\t0\n,\t0\ncarrick-on-shannon\t0\n",
                    "28\tNUMBER\n&\t0\n31\tNUMBER\nthe\t0\nstables\t0\n,\t0\nduncarbry\t0\n,\t0\ntullaghan\t0\n",
                    "jamestown\t0\napt\t0\n1\tNUMBER\n2\tNUMBER\nand\t0\n3\tNUMBER\n,\t0\njamestown\t0\n,\t0\nco\t0\nleitrim\t0\n",
                    "25a\tNUMBER\nmac\t0\nragnaill\t0\ncourt\t0\n,\t0\nlouth\t0\nrynn\t0\n,\t0\nmohill\t0\n",
                    "20\tNUMBER\n22\tNUMBER\n7\tNUMBER\n25\tNUMBER\nriverside\t0\n,\t0\nballinamore\t0\n,\t0\nco.\t0\nleitrim\t0\n",
                    "apt.\t0\n56\tNUMBER\n-\t0\nshannon\t0\nquays\t0\n,\t0\naghnahunshin\t0\n,\t0\nrooskey\t0\n",
                    "houses\t0\n1\tNUMBER\n2\tNUMBER\n3\tNUMBER\nlisnagot\t0\n,\t0\n4\tNUMBER\n5\tNUMBER\n9\tNUMBER\n10\tNUMBER\ntown\t0\nhouses\t0\nshannon\t0\ngrove\t0\n,\t0\n35a\tNUMBER\n35b\tNUMBER\n40\tNUMBER\n41\tNUMBER\nshannon\t0\ngrove\t0\n",
                    "5\tNUMBER\nwoodgreen\t0\n,\t0\ndromahair\t0\n"
                };

                foreach(string data in training_data)
                {
                    f.WriteLine(data);
                }

                break;
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

        public void useModel(string inputPath, string partial_address, ref NERAddress addr)
        {
            CRFClassifier model = CRFClassifier.getClassifierNoExceptions(inputPath);

            //string tagged_address = model.classifyToString(partial_address);
            string tagged_address = model.classifyWithInlineXML(partial_address);
            tagged_address = tagged_address.Replace("<0>", "<ZERO>");
            tagged_address = tagged_address.Replace("</0>", "</ZERO>");

            // parse xml
            XmlDocument doc = new XmlDocument();
            try
            {
                doc.LoadXml("<root>" + tagged_address + "</root>");
            }
            catch(XmlException e)
            {
                Console.WriteLine("Exception occurred while parsing xml: " + e.Message);
                return;
            }

            Console.WriteLine("Model output: " + tagged_address);

            string numbers = "";
            foreach (XmlNode node in doc.DocumentElement.ChildNodes)
            {
                if (Regex.IsMatch(node.Name, "^[IOB]-LOCALITY"))
                    addr.locality += node.InnerText + " ";

                if (Regex.IsMatch(node.Name, "^[IOB]-SECONDARY_LOCALITY"))
                    addr.secondary_locality += node.InnerText + " ";

                if (Regex.IsMatch(node.Name, "^[IOB]-THOROFARE"))
                    addr.thorofare += node.InnerText + " ";

                if (Regex.IsMatch(node.Name, "^[IOB]-BUILDING_GROUP_NAME"))
                    addr.building_group_name += node.InnerText + " ";

                if (Regex.IsMatch(node.Name, "^[IOB]-BUILDING_NAME"))
                    addr.building_name += node.InnerText + " ";

                if (Regex.IsMatch(node.Name, "^[IOB]-SUB_BUILDING_NAME"))
                    addr.sub_building_name += node.InnerText + " ";

                if (Regex.IsMatch(node.Name, "^[IOB]-BUILDING_NUMBER"))
                    addr.building_number += node.InnerText + " ";

                if (Regex.IsMatch(node.Name, "^[IOB]-DEPARTMENT"))
                    addr.department += node.InnerText + " ";

                if (Regex.IsMatch(node.Name, "^[IOB]-ORGANISATION_NAME"))
                    addr.organisation_name += node.InnerText + " ";

                if (node.Name == "NUMBER")
                    numbers += node.InnerText + " ";
            }

            addr.numbers = numbers.Split(new string[] { " " }, StringSplitOptions.RemoveEmptyEntries);

            addr.locality = addr.locality.Trim();
            addr.secondary_locality = addr.secondary_locality.Trim();
            addr.thorofare = addr.thorofare.Trim();
            addr.building_group_name = addr.building_group_name.Trim();
            addr.building_name = addr.building_name.Trim();
            addr.sub_building_name = addr.sub_building_name.Trim();
            addr.building_number = addr.building_number.Trim();
            addr.department = addr.department.Trim();
            addr.organisation_name = addr.organisation_name.Trim();
        }

        Dictionary<string[], string[]> addIOB(ref Dictionary<string, string[]> tokens)
        {
            Dictionary<string[], string[]> tagged_tokens = new Dictionary<string[], string[]>();

            foreach(KeyValuePair<string, string[]> token in tokens)
            {
                List<string> tagged_token = new List<string>();

                if (token.Value.Length == 0)
                    continue;
                else if (token.Value.Length == 1)
                {
                    tagged_token.Add("O-" + token.Key);
                }
                else
                {
                    for (int i = 0; i < token.Value.Length; i++)
                    {
                        if (i == 0)
                            tagged_token.Add("B-" + token.Key);
                        else
                            tagged_token.Add("I-" + token.Key);
                    }
                }

                tagged_tokens.Add(tagged_token.ToArray(), tokens[token.Key]);
            }

            return tagged_tokens;
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
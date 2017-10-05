
#include "Snapshots.h"


Snapshots::Snapshots( const int t_snapshotMaxNo, const std::string t_snapshotFile, const int t_snapshotFreq ){
  m_snapshotMaxNumber = t_snapshotMaxNo;
  m_snapshotFile      = t_snapshotFile;
  m_snapshotFreq      = t_snapshotFreq;
  
  load();
}

Snapshots::~Snapshots(){}
    

// =================================================================================================
// Function: Set Logger att
// =================================================================================================
void Snapshots::set_logger_att( bool toScreen, std::string logFile, int loglevel ) {
  logger = new Logger(toScreen, logFile, loglevel);
}


int Snapshots::snapshot_count(){

  int no=0;
  std::fstream myFile;
  std::string line;
  myFile.open(m_snapshotFile.c_str());
 
  if (!myFile.is_open())
    return -1;
    
    // check if file is empty
    if ( myFile.peek() == std::ifstream::traits_type::eof() )
    return 0;
    
  while (std::getline(myFile, line)){
    no++;
  }
  myFile.close(); 
  return no;
}

int Snapshots::renew() {
  // get the current time 
  std::string current_timestamp = utility::unixTime();
  
  // get the creation date of the latest snapshot
  std::string date = latest_date();
  
  int diff = stol(date) - stol(current_timestamp);
  
  if (diff < 0) diff = diff * (-1);
  
  if ( diff < ( m_snapshotFreq * 60 ) ){
    return 0; // dont renew
  } else {
    return 1;
  }
  
}


int Snapshots::create_snapshot( const std::string t_targetFilesystem, int t_frequency ){
  
  std::string output, snapshot_id;
  Snapshot snapshot;
  
  // get the current time 
  std::string current_timestamp = utility::unixTime();
  

  // create a new snapshot  
  logger->log("debug", "", "volsd", 1, "creating new snapshot ...", "create_snapshot");  
  
  int res = utility::exec(output, "aws ec2 create-snapshot --volume-id " + t_targetFilesystem + " --description \"volumed: Snapshots for the targe filesystem\" --region us-east-1 --query SnapshotId --output text");
  if (!res){
    logger->log("error", "", "volsd", 1, "Failed to create new snapshot", "create_snapshot");  
    logger->log("error", "", "volsd", 1, "AWSERROR:[" + output + "]", "create_snapshot");  
    return 0;
  }
  snapshot_id = output; 
  utility::clean_string(snapshot_id);
  
  output = "";
  utility::exec(output, "aws ec2 describe-snapshots --snapshot-id " + snapshot_id + " --region us-east-1 --query Snapshots[].State --region us-east-1 --output text");
  utility::clean_string(output);
  
  while (output.compare("pending") == 0) {
    logger->log("debug", "", "volsd", 1, "awaiting for [" + snapshot_id + "] to be ready ...", "create_snapshot");  
    output = "";
    utility::exec(output, "aws ec2 describe-snapshots --snapshot-id " + snapshot_id + " --region us-east-1 --query Snapshots[].State --region us-east-1 --output text");
    utility::clean_string(output);
    sleep(5);
  }
  
  logger->log("info", "", "volsd", 1, "Snapshot [" + snapshot_id + "] was created", "create_snapshot");  
  
  //updating snapshot file
  Snapshot s;
  s.id        = snapshot_id;
  s.timestamp = current_timestamp;
  s.status    = "idle";
  s.is_latest ="true";
  update_snapshots( s );
  
  return 1;
}


void Snapshots::print_snapshots( ) {

  if (m_snapshots.empty())
    std::cout << " There are no snapshots created yet";

  for(std::vector<Snapshot>::iterator it = m_snapshots.begin(); it != m_snapshots.end(); ++it) {
    std::cout << "VolId:["   << it->id 
              << "] statu:[" << it->timestamp 
              << "] attac:[" << it->status 
              << "] mount:[" << it->is_latest
              << "]\n";  
  
  }
}


int Snapshots::update_snapshots( const Snapshot t_snapshot ) {
  
  // 1) Appned the new snapshot to file
  m_snapshots.push_back(t_snapshot);
  
  // 2) Snapshots must not exceed the m_snapshotMaxNumber;
  if ( m_snapshots.size() > m_snapshotMaxNumber ) {
    logger->log( "debug", "", "volsd", 1, "exceeding maximum number of snapshot allowed:[" + 
                 utility::to_string(m_snapshots.size()) + "/" + 
                 utility::to_string(m_snapshotMaxNumber) + 
                 "]", 
                 "update_snapshots"
               );    
    // if so, then remove the first snapshot in the list/vector.
    std::string sId = m_snapshots[0].id;
    m_snapshots.erase (m_snapshots.begin());
    // delete from amazon space
    std::string output;
    utility::exec(output, "aws ec2 delete-snapshot --snapshot-id " + sId + " --region us-east-1");
  }
  
  // 3) write these changes to file
  write_to_file();
  
  // done
  return 1;
}


std::string Snapshots::latest_date(){
  
  logger->log("debug", "", "volsd", 1, "looking for latest snapshot", "get_latest");  
  
  if (m_snapshots.empty())
    return "654369275";
 
  // latest snapshot is the last snapshot in the vector
  //for(std::vector<Snapshot>::iterator it = m_snapshots.begin(); it != m_snapshots.end(); ++it) {
  //  if ( it->is_latest == "true" ){
  //    logger->log("debug", "", "volsd", 0, "latest snapshot timestamp was [" +  it->timestamp + "]", "get_latest");
  //    return it->timestamp;
  //  }
  //}
  return m_snapshots[ m_snapshots.size()-1 ].timestamp;
}


int Snapshots::latest( std::string &t_snapshotId ){
  
  logger->log("debug", "", "volsd", 1, "looking for latest snapshot", "get_latest");  
  
  if (m_snapshots.empty())
    return 0;
   
  logger->log("debug", "", "volsd", 1, 
              "latest snapshot id:[" + 
              m_snapshots[ m_snapshots.size()-1 ].id + "]", 
              "get_latest");
              
  t_snapshotId = m_snapshots[ m_snapshots.size()-1 ].id;
  
  return 1;
}


int Snapshots::load(){
  
  // 1) open the file, and load all volume info in the array
  std::ifstream myFile;
  std::string line;
  myFile.open(m_snapshotFile.c_str());

  if ( utility::is_empty(m_snapshotFile) ){
    return 0;
  }
      
  // 2) load data
  Snapshot s;
  while (std::getline(myFile, line)) {
      
      std::size_t id_pos = line.find(' ', 0);
      s.id = line.substr(0, id_pos);
      
      std::size_t ts_pos = line.find(' ', id_pos+1);
      s.timestamp = line.substr(id_pos+1, ts_pos - id_pos-1);
      
      std::size_t status_pos = line.find(' ', ts_pos+1);
      s.status = line.substr(ts_pos+1, status_pos-ts_pos-1);
      
      std::size_t latest_pos = line.find(' ', status_pos+1);
      s.is_latest = line.substr(latest_pos+1, status_pos-latest_pos-1);
      
      m_snapshots.push_back(s);
  }
  
  myFile.close();
  myFile.clear();
  
  // 3) Snapshots must not exceed the m_snapshotMaxNumber;
  if ( m_snapshots.size() > m_snapshotMaxNumber ) {
    // if so, then remove the first snapshot in the list/vector.
    std::string sId = m_snapshots[0].id;
    m_snapshots.erase (m_snapshots.begin());
    // delete from amazon space
    std::string output;
    utility::exec(output, "aws ec2 delete-snapshot --snapshot-id " + sId + " --region us-east-1");
  }
  
  return 0;  
}


int Snapshots::write_to_file(){
  // write back to file
  logger->log("debug", "", "volsd", 1, 
              "writing changes to snapshots file", 
              "write_to_file");
  
  std::ofstream myFileOut;
  myFileOut.open(m_snapshotFile.c_str(), std::fstream::out | std::fstream::trunc);

  if (!myFileOut.is_open()) {
    logger->log("error", "", "volsd", 1, 
                "could not open snapshots file", 
                "write_to_file");
    return false;
  }

  std::string line = "";
  
  for(std::vector<Snapshot>::iterator it = m_snapshots.begin(); it != m_snapshots.end(); ++it) {
    myFileOut << it->id << " " << it->timestamp << " " << it->status
              << " " << it->is_latest << "\n";
  }
  
  myFileOut.close();
  myFileOut.clear();
  
  return true;
}


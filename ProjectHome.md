FastDHT is a high performance distributed hash table (DHT) which based key value pairs. It can store mass key value pairs such as filename mapping, session data and user related data.

FastDHT uses the Berkeley DB as data storage to support mass data and libevent as network IO to support huge connections. FastDHT implements data replication by it's binlog file, so it only uses the basic storage function of the Berkeley DB.

FastDHT cluster composes of one or many groups and a group contains one or more servers. The data on a group is same and data is synchronized only among the servers of the same group. The servers of a group are coordinative, so which server is selected to access according to the key's hash code. The source server pushes the data to other servers in the group.

The client of FastDHT decides which server to be selected. When select which server to access, we use the key's hash code to avoid lookuping the constrast table. The step of the algorithm is:
  * calculate the hash code of the key
  * group\_index = hash\_code % group\_count
  * new\_hash\_code = (hash\_code << 16) | (hash\_code >> 16)
  * server\_index = new\_hash\_code % server\_count\_in\_the\_group

In FastDHT, the key contains three fields: namespace, object ID and key name. These three concepts are like those of the database system:
  * namespace vs database name
  * object ID vs table name
  * key name vs field name
The purpose of namespace is resolving the probable data conflict between multiple users such as the different applications or products.
The purpose of object ID is convenient for organization and management of object related data such as user's data and increasing the whole performance.
The sytem is more flexible as namespace and object ID are imported. These two fields can be empty in some cases.
The input of the hash function (it's result is hash code) is: If namespace and object ID are not empty, the concatenation of these two fields, or the key name.

FastDHT imports the concept of logic group which is used to avoid re-hashing when the system scales. A physical group is composed of one or more real servers. It can contains one or more logic groups.
A FastDHT server supports several logic groups and the data of a logic group is stored to a single Berkeley DB file.
This design provides more convenience for scaling. We can use a larger number of logic groups at the beginning. When system scaled, one or more logic groups are migrated to the new physical group(s). All things we need to do are:
  * copy the Berkeley DB data file to the new servers
  * change the config file
  * restart the programs of the FastDHT servers
  * restart the programs of the FastDHT clients if necessary

FastDHT supports timeout and every key has timeout attribute. FastDHT can be used to store session data which is more efficient and more simple than the traditional database.
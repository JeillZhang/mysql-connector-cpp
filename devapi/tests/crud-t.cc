/*
 * Copyright (c) 2015, 2024, Oracle and/or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0, as
 * published by the Free Software Foundation.
 *
 * This program is designed to work with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms, as
 * designated in a particular file or component or in included license
 * documentation. The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have either included with
 * the program or referenced in the documentation.
 *
 * Without limiting anything contained in the foregoing, this file,
 * which is part of Connector/C++, is also subject to the
 * Universal FOSS Exception, version 1.0, a copy of which can be found at
 * https://oss.oracle.com/licenses/universal-foss-exception.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "test.h"
#include <iostream>
#include <list>
#include <algorithm>
#include <set>
#include <deque>
#include <map>
#include <chrono>
#include <thread>

using std::cout;
using std::endl;
using namespace mysqlx;

class Crud : public mysqlx::test::DevAPI
{
public:

  void SetUp()
  {
    Xplugin::SetUp();

    /*
      Clear sql_mode to work around problems with how
      xplugin handles group_by queries (the "only_full_group_by"
      mode which is enabled by default).
    */
    try {
      get_sess().sql("set sql_mode=''").execute();
    }
    catch (...)
    {}
  }

  void add_data(Collection &coll);
};



TEST_F(Crud, basic)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Creating collection..." << endl;

  Schema sch = getSchema("test");
  Collection coll = sch.createCollection("c1", true);

  coll.remove("true").execute();

  {
    RowResult res = sql("select count(*) from test.c1");
    unsigned  cnt = res.fetchOne()[0];
    EXPECT_EQ(0u, cnt);
  }

  cout << "Inserting documents..." << endl;

  {
    Result add;

    DbDoc doc("{ \"name\": \"foo\", \"age\": 1 }");

    add = coll.add(doc, doc).execute();
    output_id_list(add);
    EXPECT_EQ(2U, add.getAffectedItemsCount());

    add = coll.add("{ \"name\": \"bar\", \"age\": 2 }")
      .add("{ \"name\": \"baz\", \"age\": 3, \"date\": { \"day\": 20, \"month\": \"Apr\" }}").execute();
    output_id_list(add);
    EXPECT_EQ(2U, add.getAffectedItemsCount());

    add = coll.add("{ \"_id\": \"myuuid-1\", \"name\": \"foo\", \"age\": 7 }",
      "{ \"name\": \"buz\", \"age\": 17 }").execute();
    output_id_list(add);
    EXPECT_EQ(2U, add.getAffectedItemsCount());
    EXPECT_EQ(0U, add.getAutoIncrementValue());
  }

  {
    RowResult res = sql("select count(*) from test.c1");
    unsigned  cnt = res.fetchOne()[0];
    EXPECT_EQ(6U, cnt);
  }

  cout << "Fetching documents..." << endl;

  DocResult docs = coll.find("name like 'ba%'").execute();

  DbDoc doc = docs.fetchOne();

  unsigned i = 0;
  for (; doc; ++i, doc = docs.fetchOne())
  {
    cout << "doc#" << i << ": " << doc << endl;

    for (Field fld : doc)
    {
      cout << " field `" << fld << "`: " << doc[fld] << endl;
    }

    string name = doc["name"];
    cout << " name: " << name << endl;

    if (doc.hasField("date") && Value::DOCUMENT == doc.fieldType("date"))
    {
      cout << "- date field" << endl;
      DbDoc date = doc["date"];
      for (Field fld : date)
      {
        cout << "  date `" << fld << "`: " << date[fld] << endl;
      }
      string month = doc["date"]["month"];
      int day = date["day"];
      cout << "  month: " << month << endl;
      cout << "  day: " << day << endl;
    }

    cout << endl;
  }

  EXPECT_EQ(2U, i);

  cout << "querying collection with SQL ..." << endl;

  {
    SqlResult res = sql("SELECT * FROM test.c1");

    cout << "Query sent, reading rows..." << endl;
    cout << "There are " << res.getColumnCount() << " columns in the result" << endl;

    //From server 8.0.19, 3 columns are expected
    //have 2 columns
    EXPECT_GE(res.getColumnCount(), 2U);

    Row row;
    unsigned row_count = 0;
    while ((row = res.fetchOne()))
    {
      row_count++;
      cout << "== next row ==" << endl;
      for (i = 0; i < res.getColumnCount(); ++i)
      {
        cout << "col#" << i << ": " << row[i] << endl;
      }
    }

    EXPECT_EQ(6U, row_count);
  }

  cout << "Done!" << endl;
}


TEST_F(Crud, life_time)
{
  SKIP_IF_NO_XPLUGIN;

  /*
    Create collection table with a document used for testing
    below.
  */

  {
    Collection coll = getSchema("test").createCollection("life_time", true);
    coll.remove("true").execute();
    coll.add("{ \"name\": \"bar\", \"age\": 2 }").execute();
  }

  /*
    Check that rows returned from RowResult and fields of a row
    each have its own, independent life-time.
  */

  {
    DbDoc doc;
    Value field;
    unsigned value;

    {
      Row row;

      {
        RowResult res = sql("SELECT 7,doc FROM test.life_time");
        row = res.fetchOne();
        value = row[0];

        // Note: we use group to make sure that the tmp RowResult instance
        // is deleted when we acces the row below.
      }

      field = row[0];
      doc = row[1];
    }

    // Similar, row is now deleted when we access field and doc.

    cout << "field value: " << field << endl;
    EXPECT_EQ(value, (unsigned)field);

    cout << "document: " << doc << endl;

    string name = doc["name"];
    EXPECT_EQ(2U, (unsigned)doc["age"]);
    EXPECT_EQ(string("bar"), (string)doc["name"]);
  }

}


TEST_F(Crud, add_doc_negative)
{
  SKIP_IF_NO_XPLUGIN;

  Collection coll = getSchema("test").createCollection("c1", true);

  coll.remove("true").execute();

  EXPECT_THROW(coll.remove("").execute(), mysqlx::Error);
  EXPECT_THROW(coll.modify("").set("age",1).execute(), mysqlx::Error);

  EXPECT_THROW(coll.add("").execute(), mysqlx::Error);
  EXPECT_THROW(coll.add("invaliddata").execute(), mysqlx::Error);
}



TEST_F(Crud, arrays)
{
  SKIP_IF_NO_XPLUGIN;

  Collection coll = getSchema("test").createCollection("c1", true);

  coll.remove("true").execute();

  coll.add("{ \"arr\": [ 1, 2, \"foo\", [ 3, { \"bar\" : 123 } ] ] }")
      .execute();

  cout << "Document added" << endl;

  DocResult find = coll.find().execute();
  DbDoc     doc = find.fetchOne();

  cout << "Document fetched" << endl;

  EXPECT_EQ(Value::ARRAY, doc.fieldType("arr"));

  auto arr = doc["arr"];

  cout << "arr: " << arr << endl;

  EXPECT_EQ(4U, arr.elementCount());

  unsigned pos = 0;
  for (Value val : doc["arr"])
    cout << "arr[" << pos++ << "]: " << val << endl;

  EXPECT_EQ(1, (int)arr[0]);
  EXPECT_EQ(2, (int)arr[1]);
  EXPECT_EQ(string("foo"), (string)arr[2]);
  EXPECT_EQ(Value::ARRAY, arr[3].getType());

  cout << endl << "sub array arr[3]: " << arr[3] << endl;
  pos = 0;
  for (Value val : arr[3])
    cout << "sub[" << pos++ << "]: " << val << endl;

  EXPECT_EQ(3, (int)arr[3][0]);
  EXPECT_EQ(Value::DOCUMENT, arr[3][1].getType());
  EXPECT_EQ(123, (int)arr[3][1]["bar"]);
}


void Crud::add_data(Collection &coll)
{
  coll.remove("true").execute();

  {
    RowResult res = sql("select count(*) from test.c1");
    unsigned  cnt = res.fetchOne()[0];
    EXPECT_EQ(0U, cnt);
  }

  cout << "Inserting documents..." << endl;

  Result add;

  DbDoc doc("{ \"name\": \"foo\", \"age\": 1 }");

  add = coll.add(doc, doc).execute();

  Result add2;
  add2 = coll.add("{ \"name\": \"baz\", \"age\": 3,\
                  \"birth\": { \"day\": 20, \"month\": \"Apr\" } }")
        .add("{ \"name\": \"bar\", \"age\": 2, \
                    \"food\": [\"Milk\", \"Soup\"] }")

        .execute();
  output_id_list(add2);
  output_id_list(add);

  add = coll.add("{ \"_id\": \"myuuid-1\", \"name\": \"foo\", \"age\": 7 }",
                 "{ \"name\": \"buz\", \"age\": 17 }").execute();
  output_id_list(add);

  {
    RowResult res = sql("select count(*) from test.c1");
    unsigned  cnt = res.fetchOne()[0];
    EXPECT_EQ(6U, cnt);
  }


}


TEST_F(Crud, bind)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Creating collection..." << endl;

  Schema sch = getSchema("test");
  Collection coll = sch.createCollection("c1", true);

  add_data(coll);


  cout << "Fetching documents..." << endl;

  auto find = coll.find("name like :name and age < :age");
  auto find2 = find;

  EXPECT_EQ(6U,
            find.bind("name", "%")
            .bind("age", 1000)
            .execute().count());

  EXPECT_EQ(6U,
            find2.bind("name", "%")
            .bind("age", 1000)
            .execute().count());

  EXPECT_EQ(5U,
            find.bind("name", "%")
            .bind("age", 17)
            .execute().count());

  EXPECT_EQ(3U,
            find2.bind("name", "%")
            .bind("age", 3)
            .execute().count());

  //Copying object should not use same prepared statment!
  auto find3 = find2;

  EXPECT_EQ(string("bar"),
            find3.sort("name ASC").bind("name", "%")
            .bind("age", 3)
            .execute().fetchOne()["name"].get<string>());

  EXPECT_EQ(string("foo"),
            find2.bind("name", "%")
            .bind("age", 3)
            .execute().fetchOne()["name"].get<string>());

  DocResult docs = find
                   .bind("name", "ba%")
                   .bind("age", 3)
                   .execute();

  DbDoc doc = docs.fetchOne();

  unsigned i = 0;
  for (; doc; ++i, doc = docs.fetchOne())
  {
    cout << "doc#" << i << ": " << doc << endl;

    for (Field fld : doc)
    {
      cout << " field `" << fld << "`: " << doc[fld] << endl;
    }

    string name = doc["name"];
    cout << " name: " << name << endl;

    EXPECT_EQ(string("bar"), (string)doc["name"]);

    cout << "  age: " << doc["age"] << endl;

    EXPECT_EQ(2, (int)doc["age"]);

    cout << endl;
  }

  EXPECT_EQ(1U, i);


  EXPECT_EQ(static_cast<uint64_t>(0),
            find.limit(1).offset(10).bind("name", "%")
            .bind("age", 1000)
            .execute().count());

  EXPECT_EQ(static_cast<uint64_t>(0),
            find.limit(1).offset(10).bind("name", "%")
            .bind("age", 1000)
            .execute().count());

  EXPECT_EQ(static_cast<uint64_t>(0),
            find.limit(1).offset(10).bind("name", "%")
            .bind("age", 1000)
            .execute().count());

  EXPECT_EQ(static_cast<uint64_t>(0),
            find2.limit(1).offset(10).bind("name", "%")
            .bind("age", 1000)
            .execute().count());

  EXPECT_EQ(static_cast<uint64_t>(0),
            find2.limit(1).offset(10).bind("name", "%")
            .bind("age", 1000)
            .execute().count());

  EXPECT_EQ(static_cast<uint64_t>(0),
            find2.limit(1).offset(10).bind("name", "%")
            .bind("age", 1000)
            .execute().count());


  {

    cout << "Fetching documents... using bind Documents" << endl;


    EXPECT_THROW(docs = coll.find("birth like :bday")
                     .bind("bday", DbDoc("{ \"day\": 20, \"month\": \"Apr\" }"))
                     .execute(), mysqlx::Error);

    std::cout << docs.count() << std::endl;

    docs = coll.find("birth like { \"day\": 20, \"month\": \"Apr\" }")
                     .execute();

    doc = docs.fetchOne();

    i = 0;
    for (; doc; ++i, doc = docs.fetchOne())
    {
      cout << "doc#" << i << ": " << doc << endl;

      for (Field fld : doc)
      {
        cout << " field `" << fld << "`: " << doc[fld] << endl;
      }

      string name = doc["name"];
      cout << " name: " << name << endl;

      EXPECT_EQ(string("baz"), (string)doc["name"]);

      cout << "  age: " << doc["age"] << endl;

      EXPECT_EQ(3, (int)doc["age"]);

      cout << endl;
    }


    EXPECT_EQ(1U, i);

  }

  {
    cout << "Fetching documents... using bind Arrays" << endl;

    std::list<string> food_list;
    food_list.push_back("Milk");
    food_list.push_back("Soup");

    EXPECT_THROW(
      docs = coll.find("food like :food_list")
                 .bind("food_list", Value(food_list.begin(), food_list.end()))
                 .execute();
        , mysqlx::Error);

    docs = coll.find("food like [\"Milk\", \"Soup\"]")
                     .execute();

    doc = docs.fetchOne();

    i = 0;
    for (; doc; ++i, doc = docs.fetchOne())
    {
      cout << "doc#" << i << ": " << doc << endl;

      for (Field fld : doc)
      {
        cout << " field `" << fld << "`: " << doc[fld] << endl;
      }

      string name = doc["name"];
      cout << " name: " << name << endl;

      EXPECT_EQ(string("bar"), (string)doc["name"]);

      cout << "  age: " << doc["age"] << endl;

      EXPECT_EQ(2, (int)doc["age"]);

      cout << endl;
    }

    EXPECT_EQ(1U, i);

  }


  std::map<string, Value> args;

  args["name"] = "ba%";
  args["age"] = 3;

  CollectionRemove remove(coll, "name like :name and age < :age");

  remove.bind(args).execute();

  CollectionFind find_none(coll, "name like :name and age < :age");

  docs = find_none.bind(args).execute();

  doc = docs.fetchOne();
  EXPECT_FALSE((bool)doc);


  cout << "Done!" << endl;
}


TEST_F(Crud, modify)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Creating collection..." << endl;

  Schema sch = getSchema("test");
  Collection coll = sch.createCollection("c1", true);

  add_data(coll);


  cout << "Fetching documents..." << endl;

  DocResult docs = coll.find("name like :name and age < :age")
         .bind("name", "ba%")
         .bind("age", 3)
         .execute();

  DbDoc doc = docs.fetchOne();

  unsigned i = 0;
  for (; doc; ++i, doc = docs.fetchOne())
  {
    cout << "doc#" << i << ": " << doc << endl;

    for (Field fld : doc)
    {
      cout << " field `" << fld << "`: " << doc[fld] << endl;
    }

    string name = doc["name"];
    cout << " name: " << name << endl;

    EXPECT_EQ(string("bar"), (string)doc["name"]);

    cout << "  age: " << doc["age"] << endl;

    EXPECT_EQ(2, (int)doc["age"]);

    cout << endl;
  }

  EXPECT_EQ(1U, i);

  cout << "Modify documents..." << endl;

  {
    Result res;
    auto op = coll.modify("name like :name and age < :age");
    op.set(string("name"), Value("boo"));
    op.set("$.age", expr("age+1"));
    op.arrayAppend("food", "Popcorn");

    // Note: scenario from bug#27270420

    std::string food("food");
    std::string coke("Coke");

    res = op.arrayAppend(food.c_str(), coke)
      .bind("name", "ba%")
      .bind("age", 3)
      .execute();

    EXPECT_EQ(1U, res.getAffectedItemsCount());
  }

  cout << "Fetching documents..." << endl;


  docs = coll.find("name like :name and age < :age")
         .bind("name", "bo%")
         .bind("age", 4)
         .execute();

  doc = docs.fetchOne();

  i = 0;
  for (; doc; ++i, doc = docs.fetchOne())
  {
    cout << "doc#" << i << ": " << doc << endl;

    for (Field fld : doc)
    {
      cout << " field `" << fld << "`: ";

      switch (doc[fld].getType())
      {
        case Value::ARRAY:
          {
            int elem = 0;
            cout << "[";
            for(auto it : doc[fld])
            {
              if (0 != elem)
                cout << ", ";
              cout << it;
              switch (elem)
              {
                case 0: EXPECT_EQ(string("Milk"), (string)it); break;
                case 1: EXPECT_EQ(string("Soup"), (string)it); break;
                case 2: EXPECT_EQ(string("Popcorn"), (string)it); break;
                case 3: EXPECT_EQ(string("Coke"), (string)it); break;
              }

              ++elem;
            }
            cout << "]";
          }
          break;
        default:
          cout << doc[fld];
          break;
      }
      cout << endl;
    }



    string name = doc["name"];
    cout << " name: " << name << endl;

    EXPECT_EQ(string("boo"), (string)doc["name"]);

    cout << "  age: " << doc["age"] << endl;

    // Double type because of MySQL 8.0.4 type change
      EXPECT_EQ(3, (double)doc["age"]);

    {
      CollectionModify op(coll, "name like :name");
      op.unset("food").bind("name", "bo%").execute();
    }

    docs = coll.find("name like :name")
           .bind("name", "bo%")
           .execute();

    doc = docs.fetchOne();

    EXPECT_THROW(doc["food"], std::out_of_range);

    cout << endl;
  }

}


TEST_F(Crud, order_limit)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Creating collection..." << endl;

  Schema sch = getSchema("test");
  Collection coll = sch.createCollection("c1", true);

  add_data(coll);

  DocResult docs = coll.find()
                       .sort("age ASC")
                       .limit(2)
                       .offset(4)
                       .execute();

  DbDoc doc = docs.fetchOne();

  //with the offset=4 first row is age = 7
  int prev_val = 6;

  int i = 0;
  for (; doc; ++i, doc = docs.fetchOne())
  {
    cout << "doc#" << i << ": " << doc << endl;

    EXPECT_LT(prev_val, (int)doc["age"]);
    prev_val = doc["age"];

  }


  EXPECT_EQ(2, i);

  // Modify the first line (ordered by age) incrementing 1 to the age.

  EXPECT_EQ(1U,
  coll.modify("true")
      .set("age",expr("age+1"))
      .sort("age ASC")
      .limit(1)
      .execute().getAffectedItemsCount());


  /*
    Check if modify is ok.
    name DESC because now there are 2 documents with same age,
    checking the "foo" ones and ages 1 and 2
  */

  docs = coll.find().sort("age ASC", "name DESC")
                    .limit(2)
                    .execute();

  doc = docs.fetchOne();

  i = 0;
  for (; doc; ++i, doc = docs.fetchOne())
  {
    cout << "doc#" << i << ": " << doc << endl;

    // age 1 and 2
    // Double type because of MySQL 8.0.4 type change
    EXPECT_EQ(i+1, (double)doc["age"]);

    EXPECT_EQ(string("foo"), (string)doc["name"] );

  }

  // Remove the two lines

  coll.remove("true").sort("age ASC", "name DESC")
               .limit(2)
               .execute();

  docs = coll.find().sort("age ASC", "name DESC")
                    .limit(1)
                    .execute();

  EXPECT_NE(string("foo"), (string)docs.fetchOne()["name"]);
  EXPECT_TRUE(docs.fetchOne().isNull());

}

TEST_F(Crud, projections)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Creating collection..." << endl;

  Schema sch = getSchema("test");
  Collection coll = sch.createCollection("c1", true);

  add_data(coll);

  for (unsigned round = 0; round < 4; ++round)
  {
    cout << "== round " << round << " ==" << endl;

    DocResult docs;

    std::map<std::string, std::string> proj = {
      { "age", "age" },
      { "birthYear", "2016-age" },
      { "Age1", "age" },
      { "Age2", "age" }
    };

    std::deque<string> fields;

    for (auto pair : proj)
      fields.push_back(pair.second + " AS " + pair.first);

    switch (round)
    {
    case 0:
    {
      docs = coll.find().fields(fields[0], fields[1], fields[2], fields[3])
                 .execute();

      break;
    }

    case 1:
    {
      docs = coll.find().fields(fields).execute();
      break;
    }

    case 2:
    {
      fields.push_front("first");
      fields.push_back("last");

      docs = coll.find().fields(fields.begin() + 1, fields.begin() + 5)
                 .execute();
      break;
    }

    case 3:
    {
      std::string proj_str;

      for (auto pair : proj)
      {
        if (proj_str.empty())
          proj_str = "{";
        else
          proj_str += ", ";

        proj_str += "\"" + pair.first + "\": " + pair.second;
      }

      proj_str += "}";

      docs = coll.find().fields(expr(proj_str)).execute();
      break;
    }
    }

    for (DbDoc doc = docs.fetchOne();
         !doc.isNull();
         doc = docs.fetchOne())
    {
      int rows = 0;
      for (auto col : doc)
      {
        ++rows;
        cout << col << endl;
      }
      EXPECT_EQ(4, rows);
      // Double type because of MySQL 8.0.4 type change
        EXPECT_EQ(2016 - (double)doc["age"], (double)doc["birthYear"]);
    }
  }
}


TEST_F(Crud, existence_checks)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Creating session..." << endl;

  mysqlx::Session &sess = get_sess();

  cout << "Session accepted, creating collection..." << endl;

  Schema sch = sess.getSchema("test");
  Collection coll = sch.createCollection("coll", true);

  cout << "Performing checks..." << endl;

  EXPECT_NO_THROW(sess.getSchema("no_such_schema"));
  EXPECT_THROW(sess.getSchema("no_such_schema", true), Error);
  EXPECT_NO_THROW(sch.getTable("no_such_table"));
  EXPECT_THROW(sch.getTable("no_such_table", true), Error);
  EXPECT_NO_THROW(sch.getCollection("no_such_collection"));
  EXPECT_THROW(sch.getCollection("no_such_collection", true), Error);
  EXPECT_NO_THROW(sch.getCollection("coll", true));
}


TEST_F(Crud, table)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Creating session..." << endl;

  mysqlx::Session &sess = get_sess();

  cout << "Session accepted, creating collection..." << endl;

  sql("DROP TABLE IF EXISTS test.crud_table");
  sql(
    "CREATE TABLE test.crud_table("
    "  _id VARCHAR(32),"
    "  name VARCHAR(32),"
    "  age INT"
    ")");

  Schema sch = sess.getSchema("test");
  Table tbl = sch.getTable("crud_table");
  Result res;

  //Insert values on table

  std::vector<string> cols = {"_id", "age", "name" };

  //Inserting empty list

  //Bug #25515964
  //Adding empty list shouldn't do anything
  std::list<Row> rList;
  tbl.insert("_id", "age", string("name")).rows(rList).rows(rList).execute();

  //Using containers (vectors, const char* and string)

  auto insert = tbl.insert(cols);
  insert.values("ID#1", 10, "Foo");
  insert.values("ID#2", 5 , "Bar" );
  insert.values("ID#3", 3 , "Baz");
  res = insert.execute();

  EXPECT_EQ(3U, res.getAffectedItemsCount());

  //test inserting with 1 param only
  tbl.insert("_id").values("ID#99").execute();

  //Check if values inserted are ok

  {
    auto op_select = tbl.select();
    RowResult result =  op_select.where("name like :name")
                        .bind("name", "Fo%")
                        .execute();

    //FIXME: Fix when Row::Setter is fixed
    const Row r = result.fetchOne();

    EXPECT_EQ(string("Foo"),(string)r[1]);
    EXPECT_EQ(10, (int)r[2]);
    EXPECT_EQ(true, result.fetchOne().isNull());
  }

  // Testing insert data without specifying columns

  tbl.insert().values("ID#98","SourceZ","10").execute();

  //Check if values inserted are ok

  {
    auto op_select = tbl.select();
    RowResult result =  op_select.where("name like :name")
    .bind("name", "So%")
    .execute();

    //FIXME: Fix when Row::Setter is fixed
    const Row r = result.fetchOne();

    EXPECT_EQ(string("SourceZ"),(string)r[1]);
    EXPECT_EQ(10,(int)r[2]);
    EXPECT_EQ(true, result.fetchOne().isNull());
  }

  // Update values (name and age) where name = Fo%

  auto upd = tbl.update();
  upd.set("name","Qux");
  upd.set("age",expr("age+1"));
  upd.where("name like :name");
  upd.bind("name", "Fo%");
  res = upd.execute();

  EXPECT_EQ(1U, res.getAffectedItemsCount());

  // Check if its ok

  {
    auto op_select = tbl.select();
    op_select.where("name like :name");
    op_select.bind("name", "Qu%");
    RowResult result = op_select.execute();

    //FIXME: Fix when Row::Setter is fixed
    const Row r = result.fetchOne();


    EXPECT_EQ(string("Qux"), (string)r[1]);
    EXPECT_EQ(11,(int)r[2]);
    EXPECT_EQ(true, result.fetchOne().isNull());
  }


  // Delete rows where name = Qu%

  auto rm = tbl.remove();
  rm.where("name like :name");
  rm.bind("name", "Qu%");
  res = rm.execute();

  EXPECT_EQ(1U, res.getAffectedItemsCount());

  {
    auto op_select = tbl.select();
    op_select.where("name like :name");
    op_select.bind("name", "Qu%");
    RowResult result = op_select.execute();

    Row r = result.fetchOne();

    EXPECT_EQ(true, r.isNull());

  }

  {

    sql("DROP TABLE IF EXISTS test.crud_table");
    sql(
          "CREATE TABLE test.crud_table("
          "c0 JSON,"
          "c1 INT"
          ")");

    Schema sch = sess.getSchema("test");
    Table tbl = sch.getTable("crud_table");

    res = tbl.insert("c0","c1")
             .values("{\"foo\": 1, \"bar\":\"1\"}", 1 )
             .values("{\"foo\": 2, \"bar\":\"2\"}", 2 )
             .values("{\"foo\": 3, \"bar\":\"2\"}", 3 ).execute();

    EXPECT_EQ(3U, res.getAffectedItemsCount());

    RowResult res = tbl.select("c0->$.foo", "c1").where("c0->$.foo > 1 AND c1 < 3").execute();

    Row r = res.fetchOne();

    EXPECT_EQ(2, static_cast<int>(r[0]));
    EXPECT_EQ(2, static_cast<int>(r[1]));

    res = tbl.select("c0->$.foo", "c1").where("c0->$.bar > 1 AND c1 < 3").execute();
    EXPECT_NE(1U, res.count());

    res = tbl.select("c0->>$.bar", "c1").where("c0->>$.bar > 1 AND c1 < 3").execute();

    EXPECT_EQ(1U, res.count());

    r = res.fetchOne();

    EXPECT_EQ("2", static_cast<std::string>(r[0]));
    EXPECT_EQ(2, static_cast<int>(r[1]));

  }

  // Check generated auto-increment values

  {

    sql("DROP TABLE IF EXISTS test.crud_table");
    sql(
      "CREATE TABLE test.crud_table("
      "c0 JSON,"
      "c1 INT AUTO_INCREMENT,"
      "PRIMARY KEY (c1)"
      ")");

    Schema sch = sess.getSchema("test");
    Table tbl = sch.getTable("crud_table");

    res = tbl.insert("c0")
      .values("{\"foo\": 1}")
      .values("{\"foo\": 2}")
      .values("{\"foo\": 3}").execute();

    EXPECT_EQ(1U, res.getAutoIncrementValue());
    EXPECT_EQ(3U, res.getAffectedItemsCount());

    res = tbl.insert("c0")
      .values("{\"foo\": 4}")
      .values("{\"foo\": 5}").execute();

    EXPECT_EQ(4U, res.getAutoIncrementValue());
    EXPECT_EQ(2U, res.getAffectedItemsCount());

    RowResult res = tbl.select("c0->$.foo", "c1").execute();

    for (Row r; (r = res.fetchOne());)
      EXPECT_EQ((int)r[0], (int)r[1]);

  }

}


TEST_F(Crud, table_order_limit)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Creating table..." << endl;

  sql("DROP TABLE IF EXISTS test.crud_table");
  sql(
    "CREATE TABLE test.crud_table("
    "  _id VARCHAR(32),"
    "  name VARCHAR(32),"
    "  age INT"
    ")");

  Schema sch = getSchema("test");
  Table tbl = sch.getTable("crud_table");


  //Insert values on table

  std::vector<string> cols = {"_id", "age", "name" };
  //Using containers (vectors, const char* and string)
  auto insert = tbl.insert(cols);
  insert.values("ID#1", 10, "Foo");
  insert.values("ID#2", 5 , "Bar" );
  insert.values("ID#3", 3 , "Baz");
  insert.execute();

  {
    RowResult result = tbl.select().orderBy("age ASC")
                                .limit(1)
                                .offset(1)
                                .execute();

    Row r = result.fetchOne();

    EXPECT_EQ(5, (int)r[2]);
    EXPECT_TRUE(result.fetchOne().isNull());
  }

  tbl.update().set("age", expr("age+1"))
              .orderBy("age ASC")
              .limit(1)
              .execute();

  {
    RowResult result = tbl.select().orderBy("age ASC")
                                   .limit(1)
                                   .execute();

    Row r = result.fetchOne();
    EXPECT_EQ(4, (int)r[2]);
    EXPECT_TRUE(result.fetchOne().isNull());
  }

  tbl.remove()
     .where("age > 4")
     .orderBy("age DESC")
     .limit(1)
     .execute();


  {
    RowResult result = tbl.select()
                          .where("age > 4")
                          .orderBy("age DESC")
                          .limit(1)
                          .execute();

    Row r = result.fetchOne();
    EXPECT_EQ(5, (int)r[2]);
    EXPECT_TRUE(result.fetchOne().isNull());
  }
}


TEST_F(Crud, table_projections)
{

  SKIP_IF_NO_XPLUGIN;

  cout << "Creating table..." << endl;

  sql("DROP TABLE IF EXISTS test.crud_table");
  sql(
    "CREATE TABLE test.crud_table("
    "  _id VARCHAR(32),"
    "  name VARCHAR(32),"
    "  age INT"
    ")");

  Schema sch = getSchema("test");
  Table tbl = sch.getTable("crud_table");


  //Insert values on table

  auto insert = tbl.insert("_id", "age", string("name"));
  insert.values("ID#1", 10, "Foo");
  insert.values("ID#2", 5 , "Bar" );
  insert.values("ID#3", 3 , "Baz");
  insert.execute();

  std::vector<string> fields;
  fields.push_back("age");
  fields.push_back("2016-age AS birth_year");
  fields.push_back("age AS dummy");

  RowResult result = tbl.select(fields)
                     .orderBy("age ASC")
                     .execute();

  for (Row r = result.fetchOne(); !r.isNull(); r = result.fetchOne())
  {
    EXPECT_EQ(3U, r.colCount());
    EXPECT_EQ(2016-static_cast<int>(r[0]), static_cast<int>(r[1]));
  }

}


/*
  Test move semantics for CRUD operation objects.
*/
#if 0
TEST_F(Crud, move)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Creating session..." << endl;

  Session sess(this);

  Schema sch = sess.getSchema("test");
  Collection coll = sch.createCollection("coll",true);

  cout << endl;
  cout << "Collection.add 1" << endl;

  {
    auto a = coll.add("{\"foo\" : 7}");
    auto b = a;

    EXPECT_THROW(a.add(""), Error);
    EXPECT_NO_THROW(b.add(""));
  }

  cout << "Collection.add 2" << endl;

  {
    auto a = coll.add("{\"foo\" : 7}");
    auto b = a.add("");

    EXPECT_THROW(a.add(""), Error);
    EXPECT_NO_THROW(b.add(""));
  }

  cout << "Collection.add 3" << endl;

  {
    CollectionAdd a = coll.add("{\"foo\" : 7}");
    CollectionAdd b = a.add("");

    EXPECT_THROW(a.add(""), Error);
    EXPECT_NO_THROW(b.add(""));
  }

  cout << endl;
  cout << "Collection.find 1" << endl;

  {
    auto a = coll.find();
    auto b = a;

    EXPECT_THROW(a.execute(), Error);
    EXPECT_NO_THROW(b.execute());
  }

  cout << "Collection.find 2" << endl;

  {
    CollectionFind a = coll.find();
    CollectionFind b = a;

    EXPECT_THROW(a.execute(), Error);
    EXPECT_NO_THROW(b.execute());
  }

  cout << "Collection.find 3" << endl;

  {
    CollectionFind a = coll.find("foo = 7");
    CollectionFind b = a;

    EXPECT_THROW(a.execute(), Error);
    EXPECT_NO_THROW(b.execute());
  }

  cout << endl;
  cout << "Collection.modify 1" << endl;

  {
    auto a = coll.modify();
    auto b = a;

    EXPECT_THROW(a.set("",7), Error);
    EXPECT_NO_THROW(b.set("",7));
  }

  cout << "Collection.modify 2" << endl;

  {
    auto a = coll.modify();
    auto b = a.unset("");

    EXPECT_THROW(a.set("", 7), Error);
    EXPECT_NO_THROW(b.set("", 7));
  }

  cout << "Collection.modify 3" << endl;

  {
    CollectionModify a = coll.modify();
    CollectionModify b = a.unset("");

    EXPECT_THROW(a.set("", 7), Error);
    EXPECT_NO_THROW(b.set("", 7));
  }

  cout << endl;
  cout << "Collection.remove 1" << endl;

  {
    auto a = coll.remove();
    auto b = a;

    EXPECT_THROW(a.execute(), Error);
    EXPECT_NO_THROW(b.execute());
  }

  cout << "Collection.remove 2" << endl;

  {
    CollectionRemove a = coll.remove();
    CollectionRemove b = a;

    EXPECT_THROW(a.execute(), Error);
    EXPECT_NO_THROW(b.execute());
  }

  cout << endl;
}

#endif


TEST_F(Crud, doc_path)
{

  SKIP_IF_NO_XPLUGIN;

  cout << "Creating collection..." << endl;

  Schema sch = getSchema("test");
  sch.dropCollection("coll");
  Collection coll = sch.createCollection("coll",false);

  coll.add( "{\"date\": {\"monthName\":\"December\", \"days\":[1,2,3]}}").execute();

  coll.modify("true").set("date.monthName", "February" ).execute();
  coll.modify("true").set("$.date.days[0]", 4 ).execute();

  DocResult docs = coll.find().execute();

  DbDoc doc = docs.fetchOne();

  EXPECT_EQ(string("February"), static_cast<string>(doc["date"]["monthName"]));
  EXPECT_EQ(4, static_cast<int>(doc["date"]["days"][0]));

  coll.modify("true").unset("date.days[0]").execute();
  docs = coll.find().execute();
  doc = docs.fetchOne();
  EXPECT_EQ(2, static_cast<int>(doc["date"]["days"][0]));

  coll.modify("true").unset("date.days").execute();
  docs = coll.find().execute();
  doc = docs.fetchOne();
  EXPECT_THROW(static_cast<int>(doc["date"]["days"][0]), std::out_of_range);

}


TEST_F(Crud, row_error)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Creating table..." << endl;

  sql("DROP TABLE IF EXISTS test.row_error");
  sql(
    "CREATE TABLE test.row_error("
    "  _id VARCHAR(32),"
    "  age BIGINT"
    ")");

  Schema sch = getSchema("test");
  Table tbl = sch.getTable("row_error");

  //Insert values on table

  auto insert = tbl.insert("_id", "age");
  insert.values("ID#1", (int64_t)-9223372036854775807LL);
  insert.values("ID#3", (int64_t)9223372036854775805LL);
  insert.values("ID#4", (int64_t)9223372036854775806LL);
  insert.execute();

  //Overflow on second line
  {
    auto op_select = tbl.select("100000+age AS newAge");
    RowResult result =  op_select.execute();

    std::vector<Row> rows;
    try {

      for(rows.push_back(result.fetchOne());
          !rows.back().isNull();
          rows.push_back(result.fetchOne()))
      {}

      FAIL() << "Should stop after first row";
    }
    catch (mysqlx::Error& e)
    {
      cout << "Expected error " << e << endl;
    }
    EXPECT_EQ(1U, rows.size());
    std::cout << rows[0][0] << std::endl;
  }

  //Everything should work as expected if dropped
  {
    auto op_select = tbl.select("100000+age");
    RowResult result =  op_select.execute();
  }
}


TEST_F(Crud, coll_as_table)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Creating collection..." << endl;

  Schema sch = getSchema("test");
  Collection coll = sch.createCollection("coll", true);

  // Clean up
  coll.remove("true").execute();

  // Add Doc to collection
  DbDoc doc("{ \"name\": \"foo\", \"age\": 1 }");

  coll.add(doc, doc).execute();

  // Get Collectionas Table
  Table tbl = sch.getCollectionAsTable("coll");

  // Check if we can get result from collection using table
  RowResult tblResult = tbl.select("doc->$.name").execute();
  Row r = tblResult.fetchOne();
  EXPECT_EQ(string("foo"), static_cast<string>(r[0]));

  // Update Collection using Table
  tbl.update().set("doc->$.name", "bar").execute();

  // Check if it was successful
  tblResult = tbl.select("doc->$.name").execute();

  r = tblResult.fetchOne();

  EXPECT_EQ(string("bar"), static_cast<string>(r[0]));

  // Check same result with Collection obj
  DocResult docres = coll.find().fields(expr("{\"name\": name, \"age\":age+1}")).execute();

  doc = docres.fetchOne();

  EXPECT_EQ(string("bar"), static_cast<string>(doc["name"]));
  // Double type because of MySQL 8.0.4 type change
    EXPECT_EQ(2, static_cast<double>(doc["age"]));

  sql("DROP TABLE IF EXISTS test.not_collection");
  sql(
    "CREATE TABLE test.not_collection("
    "  _id VARCHAR(32),"
    "  age BIGINT"
    ")");

  // Should throw exception if its not a collection
  try {
    sch.getCollectionAsTable("not_collection");
    FAIL() << "Should throw error because this is not a collection";
  } catch (Error &) {
  }

  // Should NOT exception if its not a collection
  try {
    sch.getCollectionAsTable("not_collection", false);
  } catch (Error &) {
    FAIL() << "Should throw error because this is not a collection";
  }


}


TEST_F(Crud, get_ids)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Creating collection..." << endl;

  Schema sch = getSchema("test");
  Collection coll = sch.createCollection("coll", true);

  // Clean up
  coll.remove("true").execute();


  // Add Doc to collection
  DbDoc doc1("{ \"name\": \"foo\", \"age\": 1 }");
  DbDoc doc2("{ \"_id\":\"ABCDEFGHIJKLMNOPQRTSUVWXYZ012345\","
             " \"name\": \"bar\", \"age\": 2 }");

  Result res;
  res = coll.add(doc1).execute();

  std::vector<std::string> ids= res.getGeneratedIds();
  ASSERT_EQ(1U, ids.size());

  res = coll.remove("true").execute();

  // This functions can only be used on add() operations
  ids= res.getGeneratedIds();
  EXPECT_EQ(0U, ids.size());

  res = coll.add(doc1).add(doc2).execute();
  ids= res.getGeneratedIds();
  EXPECT_EQ(1U, ids.size());

}


TEST_F(Crud, count)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Creating collection..." << endl;

  Schema sch = getSchema("test");
  Collection coll = sch.createCollection("coll", true);

  //Remove all rows
  coll.remove("true").execute();

  {
    CollectionAdd add(coll);

    for (int i = 0; i < 1000; ++i)
    {
      std::stringstream json;
      json << "{ \"name\": \"foo\", \"age\":" << i << " }";

      add.add(json.str());
    }

    add.execute();
  }

  EXPECT_EQ(1000U, coll.count());

  coll.remove("true").limit(500).execute();

  Table tbl = sch.getCollectionAsTable("coll");

  EXPECT_EQ(500U, tbl.count());

}


TEST_F(Crud, buffered)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Creating collection";
  std::flush(cout);

  Schema sch = getSchema("test");
  Collection coll = sch.createCollection("coll", true);

  coll.remove("true").execute();

  for (int j = 0; j < 10; ++j)
  {
    CollectionAdd add(coll);
    for (int i = 0; i < 1000; ++i)
    {
      std::stringstream json;
      json << "{ \"name\": \"foo\", \"age\": " << 1000*j + i << " }";
      add.add(json.str());
    }
    add.execute();
    cout << ".";
    std::flush(cout);
  }

  cout << " done" << endl;

  {
    DocResult res = coll.find().sort("age").execute();

    //Get first directly
    DbDoc r = res.fetchOne();
    EXPECT_EQ(0, static_cast<int>(r["age"]));

    EXPECT_EQ(9999U, res.count());

    //Get second from cache, after count()
    EXPECT_EQ(1, static_cast<int>(res.fetchOne()["age"]));

    cout << "Loading all documents...";
    std::flush(cout);

    std::vector<DbDoc> rows = res.fetchAll();

    cout << " done" << endl;

    EXPECT_EQ(9998U, rows.size());

    cout << "Examining documents";
    std::flush(cout);

    auto row = rows.begin();
    int i = 2;
    for( ; row != rows.end() ; ++row, ++i)
    {
      EXPECT_EQ(i, static_cast<int>((*row)["age"]));
      if (0 != i % 1000)
        continue;
      cout << ".";
      std::flush(cout);
    }

    cout << " done" << endl;

    EXPECT_EQ(0U, res.count());

    std::vector<DbDoc> rows_empty = res.fetchAll();

    EXPECT_EQ(0U, rows_empty.size());

  }

  {
    Table tbl = sch.getCollectionAsTable("coll");

    RowResult res = tbl.select("doc->$.age AS age")
                    .orderBy("doc->$.age")
                    .execute();

    //Get first directly
    Row r = res.fetchOne();

    EXPECT_EQ(0, static_cast<int>(r[0]));

    EXPECT_EQ(9999U, res.count());

    //Get second from cache, after count()
    EXPECT_EQ(1, static_cast<int>(res.fetchOne()[0]));

    cout << "Loading all rows...";
    std::flush(cout);

    std::vector<Row> rows = res.fetchAll();

    cout << " done" << endl;

    EXPECT_EQ(9998U, rows.size());

    cout << "Examining rows";
    std::flush(cout);

    auto row = rows.begin();
    int i = 2;
    for( ; row != rows.end() ; ++row, ++i)
    {
      EXPECT_EQ(i, static_cast<int>((*row)[0]));
      if (0 != i % 1000)
        continue;
      cout << ".";
      std::flush(cout);
    }

    cout << " done" << endl;

    EXPECT_EQ(0U, res.count());

    std::vector<Row> rows_empty = res.fetchAll();

    EXPECT_EQ(0U, rows_empty.size());

  }


}


TEST_F(Crud, iterators)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Creating collection..." << endl;

  Schema sch = getSchema("test");
  Collection coll = sch.createCollection("coll", true);

  coll.remove("true").execute();

  {
    CollectionAdd add(coll);

    for (int i = 0; i < 1000; ++i)
    {
      std::stringstream json;
      json << "{ \"name\": \"foo\", \"age\":" << i << " }";

      add.add(json.str());
    }

    add.execute();
  }

  {
    DocResult res = coll.find().sort("age").execute();

    int age = 0;
    for( DbDoc doc : res)
    {
      EXPECT_EQ(age, static_cast<int>(doc["age"]));

      ++age;

      //break the loop
      if (age == 500)
        break;
    }

    EXPECT_EQ(500, age);

    //get the other 500
    for( DbDoc doc : res.fetchAll())
    {
      EXPECT_EQ(age, static_cast<int>(doc["age"]));

      ++age;
    }

    EXPECT_EQ(1000, age);

  }

  {
    Table tbl = sch.getCollectionAsTable("coll");

    RowResult res = tbl.select("doc->$.age AS age")
                    .orderBy("doc->$.age")
                    .execute();

    int age = 0;
    for( Row row : res)
    {
      EXPECT_EQ(age, static_cast<int>(row[0]));

      ++age;

      //break the loop
      if (age == 500)
        break;
    }

    EXPECT_EQ(500, age);

    //get the other 500
    for( Row row : res.fetchAll())
    {
      EXPECT_EQ(age, static_cast<int>(row[0]));

      ++age;

    }

    EXPECT_EQ(1000, age);

  }
}


TEST_F(Crud, diagnostic)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Preparing table..." << endl;

  mysqlx::Session &sess = get_sess();

  sess.sql("DROP TABLE IF EXISTS test.t").execute();
  sess.sql("CREATE TABLE test.t (a TINYINT NOT NULL, b CHAR(4))").execute();

  Table t = sess.getSchema("test").getTable("t");

  cout << "Table ready..." << endl;

  /*
    The following statement clears the default SQL mode in
    which all warnings are upgraded to errors.
  */

  sess.sql("SET SESSION sql_mode=''").execute();

  cout << "Inserting rows into the table..." << endl;

  // This insert statement should generate warnings

  Result res = t.insert().values(10, "mysql").values(300, "xyz").execute();

  for (Warning w : res.getWarnings())
  {
    cout << w << endl;
  }

  EXPECT_EQ(2U, res.getWarningsCount());

  std::vector<Warning> warnings = res.getWarnings();

  for (unsigned i = 0; i < res.getWarningsCount(); ++i)
  {
    EXPECT_EQ(warnings[i].getCode(), res.getWarning(i).getCode());
  }
}


TEST_F(Crud, cached_results)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Preparing table..." << endl;

  Collection coll = get_sess().createSchema("test", true)
                              .createCollection("test", true);

  coll.remove("true").execute();

  coll.add("{\"user\":\"Foo\"}").execute();
  coll.add("{\"user\":\"Bar\"}").execute();
  coll.add("{\"user\":\"Baz\"}").execute();

  auto coll_op = coll.find();
  auto coll_op2 = coll.find();

  DocResult coll_res = coll_op.execute();
  DocResult coll_res2 = coll_op2.execute();

  DbDoc coll_row = coll_res.fetchOne();
  DbDoc coll_row2 = coll_res2.fetchOne();

  for (; coll_row && coll_row2;
       coll_row = coll_res.fetchOne(),
       coll_row2 = coll_res2.fetchOne())
  {
    EXPECT_EQ(static_cast<string>(coll_row["user"]),
              static_cast<string>(coll_row2["user"]));

    std::cout << "User: " << coll_row["user"] << std::endl;
  }

}


TEST_F(Crud, add_empty)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Session accepted, creating collection..." << endl;

  Schema sch = getSchema("test");
  Collection coll = sch.createCollection("c1", true);

  coll.remove("true").execute();

  //Check bug when Result was created uninitialized
  Result add;

  //Adding Empty docs throws Error
//  EXPECT_THROW(add = coll.add(nullptr).execute(),
//               mysqlx::Error);
  EXPECT_THROW(add = coll.add(static_cast<char*>(nullptr)).execute(),
               mysqlx::Error);
}


TEST_F(Crud, group_by_having)
{
  SKIP_IF_NO_XPLUGIN;
  SKIP_IF_SERVER_VERSION_LESS(5,7,19);

  cout << "Preparing table..." << endl;

  Schema test = get_sess().createSchema("test", true);
  Collection coll = test.createCollection("coll", true);
  Table tbl = test.getCollectionAsTable("coll", true);

  coll.remove("true").execute();

  std::vector<string> names = { "Foo", "Baz", "Bar" };

  int i=0;

  for (auto name : names)
  {
    std::stringstream json;
    json <<"{ \"_id\":\""<< i << "\", \"user\":\"" << name
        << "\", \"birthday\": { \"day\":" << 20+i << "}}";
    coll.add(json.str()).execute();
    ++i;
  }

  // Move "Foo" (with age 20) to the end.

  std::sort(names.begin(), names.end());

  // Function to check results of operation
  auto check_results = [&names] (DocResult &coll_res, RowResult &tbl_res)
  {
    std::set<string> cset{ names.begin(), names.end() };
    std::set<string> tset{ names.begin(), names.end() };

    DbDoc coll_row = coll_res.fetchOne();
    Row tbl_row = tbl_res.fetchOne();

    for (; coll_row && tbl_row ;
         coll_row = coll_res.fetchOne(),
         tbl_row = tbl_res.fetchOne())
    {
      EXPECT_EQ(1U,cset.erase(coll_row["user"].get<string>()));
      EXPECT_EQ(1U, tset.erase(tbl_row[0].get<string>()));
    }

    EXPECT_TRUE(cset.empty());
    EXPECT_TRUE(tset.empty());
  };

  auto coll_res = coll.find().fields("user AS user", "birthday.day as bday").execute();
  auto tbl_res = tbl.select("doc->$.user as user","doc->$.birthday.day as bday").execute();

  check_results(coll_res, tbl_res);

  cout << "Check with groupBy" << endl;

  std::vector<string> fields = {"user", "bday" };
  coll_res = coll.find()
             .fields("user AS user", "birthday.day as bday")
             .groupBy(fields)
             .execute();

  cout << "and on table" << endl;
  tbl_res = tbl.select("doc->$.user as user", "doc->$.birthday.day as bday")
               .groupBy("user", "bday")
               .execute();


  check_results(coll_res, tbl_res);


  cout << "Having usage will remove last name for the list." << endl;
  names.pop_back();

  coll_res = coll.find()
             .fields(expr(R"({"user": user, "bday": { "date": birthday}})"))
             .groupBy("user", "birthday")
             .having("bday.date.day > 20")
             .execute();

  //and on table
  tbl_res = tbl.select("doc->$.user as user", "doc->$.birthday as bday")
            .groupBy(fields)
            .having("bday->$.day > 20")
            .execute();

  check_results(coll_res, tbl_res);

  cout << "Same test but passing std::string to groupBy" << endl;

  coll_res = coll.find()
             .fields("user AS user", "birthday.day as bday")
             .groupBy(std::string("user"), std::string("bday"))
             .having(std::string("bday > 20"))
             .execute();

  cout << "and on table" << endl;
  tbl_res = tbl.select("doc->$.user as user", "doc->$.birthday.day as bday")
            .groupBy(fields)
            .having(std::string("bday > 20"))
            .orderBy("user")
            .execute();

  check_results(coll_res, tbl_res);

}


TEST_F(Crud, copy_semantics)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Creating collection..." << endl;

  Schema sch = getSchema("test");
  Collection coll = sch.createCollection("c1", true);

  add_data(coll);


  cout << "Fetching documents..." << endl;

  CollectionFind find = coll.find("name like :name and age < :age");
  find.bind("name", "ba%");
  find.bind("age", 3);

  CollectionFind find2 = find;

  DocResult docs = find2.execute();

  DbDoc doc = docs.fetchOne();

  unsigned i = 0;
  for (; doc; ++i, doc = docs.fetchOne())
  {
    cout << "doc#" << i << ": " << doc << endl;

    for (Field fld : doc)
    {
      cout << " field `" << fld << "`: " << doc[fld] << endl;
    }

    string name = doc["name"];
    cout << " name: " << name << endl;

    EXPECT_EQ(string("bar"), (string)doc["name"]);

    cout << "  age: " << doc["age"] << endl;

    EXPECT_EQ(2, (int)doc["age"]);

    cout << endl;
  }

  EXPECT_EQ(1U, i);

  std::map<string, Value> args;

  args["name"] = "ba%";
  args["age"] = 3;

  CollectionRemove remove(coll, "name like :name and age < :age");

  remove.bind(args);

  CollectionRemove remove2 = remove;

  remove2.execute();

  {
    CollectionFind f(coll, "name like :name and age < :age");

    CollectionFind find2 = f;

    find2.bind(args);

    docs = find2.execute();

    doc = docs.fetchOne();
    EXPECT_FALSE((bool)doc);
  }


  cout << "Done!" << endl;
}


TEST_F(Crud, multi_statment_exec)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Creating collection..." << endl;

  Schema sch = getSchema("test");
  Collection coll = sch.createCollection("c1", true);

  add_data(coll);

  auto find = coll.find("age = :age");

  auto test = [] (DocResult& res, int age)
  {
     DbDoc doc = res.fetchOne();
     EXPECT_EQ(age, static_cast<int>(doc["age"]));

     doc = res.fetchOne();

     EXPECT_TRUE(!doc);
  };

  find.bind("age", 2);

  auto res_2 = find.execute();

  auto res_3 = find.bind("age", 3).execute();

  test(res_3, 3);

  {
    auto res = find.bind("age", 2).execute();

    test(res, 2);
  }

  {
    auto res = find.bind("age", 3).execute();

    test(res, 3);
  }

  test(res_2, 2);

  auto remove = coll.remove("age = :age");

  auto remove1 = remove.bind("age",3);


  auto remove2 = remove.bind("age",2);

  remove2.execute();

  remove1.execute();

}


TEST_F(Crud, expr_in_expr)
{
  SKIP_IF_NO_XPLUGIN;

  SKIP_IF_SERVER_VERSION_LESS(8, 0, 2);

  cout << "Creating collection..." << endl;

  Schema sch = getSchema("test");
  Collection coll = sch.createCollection("c1", true);

  add_data(coll);

  auto res = coll.find("{\"name\":\"baz\"} in $").execute();

  EXPECT_EQ( string("baz") , (string)res.fetchOne()["name"]);

  EXPECT_TRUE(res.fetchOne().isNull());

  res = coll.find("'bar' in $.name").execute();

  EXPECT_EQ( string("bar") , (string)res.fetchOne()["name"]);

  EXPECT_TRUE(res.fetchOne().isNull());

  res = coll.find("{ \"day\": 20, \"month\": \"Apr\" } in $.birth").execute();

  EXPECT_EQ( string("baz") , (string)res.fetchOne()["name"]);

  EXPECT_TRUE(res.fetchOne().isNull());

  res = coll.find("JSON_TYPE($.food) = 'ARRAY' AND 'Milk' IN $.food ").execute();

  EXPECT_EQ( string("bar") , (string)res.fetchOne()["name"]);

  EXPECT_TRUE(res.fetchOne().isNull());

  auto tbl = sch.getTable("c1");

  auto tbl_res = tbl.select("JSON_EXTRACT(doc,'$.name') as name").where("{\"name\":\"baz\"} in doc->$").execute();
  EXPECT_EQ( string("baz") , (string)tbl_res.fetchOne()[0]);

}


TEST_F(Crud, row_locking)
{
  SKIP_IF_NO_XPLUGIN;

  mysqlx::Session &sess = get_sess();
  SKIP_IF_SERVER_VERSION_LESS(8, 0, 3)

  string db_name = "row_locking";
  string tab_name = "row_lock_tab";
  string coll_name = "row_lock_coll";

  try
  {
    sess.dropSchema(db_name);
  }
  catch(...) {}

  sess.createSchema(db_name);

  std::stringstream strs;
  strs << "CREATE TABLE " << db_name << "." << tab_name
       << "(id int primary key) ENGINE InnoDB";

  sql(strs.str());

  Schema sch = sess.getSchema(db_name);
  Table tbl = sch.getTable(tab_name);

  tbl.insert()
    .values(1)
    .values(2)
    .values(3).execute();

  sess.startTransaction();
  RowResult res = tbl.select().lockExclusive().execute();
  for (Row r; (r = res.fetchOne());)
    cout << r[0] << endl;

  auto lock_check = sess.getSchema("information_schema")
            .getTable("innodb_trx")
            .select("count(trx_rows_locked)")
            .where("trx_mysql_thread_id=connection_id()");


  /* Some number of rows has to be locked */
  EXPECT_TRUE((int)lock_check.execute().fetchOne()[0] > 0);
  sess.commit();

  /* Wait to for the row locking status to populate */
  std::this_thread::sleep_for(std::chrono::milliseconds(5000));
  /* No rows here */
  EXPECT_TRUE((int)lock_check.execute().fetchOne()[0] == 0);

  sch.createCollection(coll_name);
  Collection coll = sch.getCollection(coll_name);
  coll.add("{ \"num\": 1 }")
      .add("{ \"num\": 2 }")
      .add("{ \"num\": 3 }")
      .execute();

  sess.startTransaction();
  DocResult dres = coll.find().lockExclusive().execute();

  for (DbDoc d; (d = dres.fetchOne());)
    cout << d["num"] << endl;

  /* Wait to for the row locking status to populate */
  std::this_thread::sleep_for(std::chrono::milliseconds(5000));
  /* Some number of rows has to be locked */
  EXPECT_TRUE((int)lock_check.execute().fetchOne()[0] > 0);
  sess.commit();

  /* Wait to for the row locking status to populate */
  std::this_thread::sleep_for(std::chrono::milliseconds(5000));
  /* No rows here */
  EXPECT_TRUE((int)lock_check.execute().fetchOne()[0] == 0);

  sess.dropSchema(db_name);
}


TEST_F(Crud, lock_contention)
{
  SKIP_IF_NO_XPLUGIN;

  auto &sess = get_sess();

  //decrease the lock wait time (default = 50s)
  sql("set session innodb_lock_wait_timeout = 5");
  sql("set global innodb_lock_wait_timeout = 5");

  Schema sch = getSchema("test");
  Collection coll = sch.createCollection("c1", true);
  Table tbl = sch.getCollectionAsTable("c1", true);

  coll.remove("true").execute();


  for(int i = 0; i < 10; ++i)
  {
    std::stringstream doc;
    doc << R"({"name":"Luis", "_id":)" << i+1 << "}";
    coll.add(DbDoc(doc.str())).execute();
  }

  /*
    First session lock the rows, second one, tries to read/write values
  */
  Session s_nolock(this);
  Schema sch_nolock = s_nolock.getSchema("test");
  Collection coll_nolock = sch_nolock.getCollection("c1", true);
  Table tbl_nolock = sch_nolock.getCollectionAsTable("c1");

  sess.startTransaction();
  s_nolock.startTransaction();

  auto res_id2 = std::make_shared<RowResult>(
      tbl.select().where("_id like '2'").lockExclusive().execute());

  EXPECT_EQ(1U, res_id2->count());

  EXPECT_EQ(9U,
            tbl_nolock.select()
            .lockExclusive(mysqlx::LockContention::SKIP_LOCKED)
            .execute()
            .count());

  EXPECT_EQ(9U,
            coll_nolock.find()
            .lockExclusive(mysqlx::LockContention::SKIP_LOCKED)
            .execute()
            .count());

  auto select_error = tbl_nolock.select().lockExclusive(mysqlx::LockContention::NOWAIT);

  EXPECT_THROW(select_error.execute().count(), Error);
  try
  {
    std::vector<Row> rows = select_error.execute().fetchAll();
    FAIL() << "Should throw error!";
  }
  catch (Error &e)
  {
    cout << "Expected: " << e << endl;
  }

  auto find_error = coll_nolock.find().lockExclusive(mysqlx::LockContention::NOWAIT);

  EXPECT_THROW(find_error.execute().count(), Error);
  try
  {
    std::vector<DbDoc> rows = find_error.execute().fetchAll();
    FAIL() << "Should throw error!";
  }
  catch (Error &e)
  {
    cout << "Expected: " << e << endl;
  }

  auto res_error = std::make_shared<RowResult>(select_error.execute());

  try{
    for (Row row : *res_error)
    {
      std::cout << row << std::endl;
    }
    FAIL() << "Should throw error";
  }
  catch(mysqlx::Error&)
  {}

  auto coll_res_error = std::make_shared<DocResult>(find_error.execute());

  try{
    for (auto doc : *coll_res_error)
    {
      std::cout << doc << std::endl;
    }
    FAIL() << "Should throw error";
  }
  catch(mysqlx::Error&)
  {}

  sess.rollback();

  s_nolock.rollback();

  /*
    Shared lock tests
  */

  sess.startTransaction();
  s_nolock.startTransaction();

  auto res_id3 = std::make_shared<RowResult>(
      tbl.select().where("_id like '3'").lockShared().execute());

  EXPECT_EQ(10U,
            tbl_nolock.select().lockShared(mysqlx::LockContention::SKIP_LOCKED)
            .execute().count());

  EXPECT_EQ(10U,
            coll_nolock.find().lockShared(mysqlx::LockContention::SKIP_LOCKED)
            .execute().count());

  EXPECT_EQ(10U,
            tbl_nolock.select().lockShared(mysqlx::LockContention::NOWAIT)
            .execute().count());

  EXPECT_EQ(10U,
            coll_nolock.find().lockShared(mysqlx::LockContention::NOWAIT)
            .execute().count());

  //Should timeout!
  EXPECT_THROW(coll_nolock.modify("true").set("name",L"Rafał").execute(), Error);

  std::thread thread_modify([&] {
   coll_nolock.modify("true").set("name",L"Rafał").execute();
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(1000));

  sess.rollback();

  // Let's free all locks
  res_id2.reset();
  res_error.reset();
  coll_res_error.reset();
  res_id3.reset();

  thread_modify.join();

  s_nolock.rollback();

}


TEST_F(Crud, single_document)
{
  SKIP_IF_NO_XPLUGIN;

  /*
    Note: requires x-protocol support for 'upsert' flag and WL#10682
    (Mysqlx.CRUD.Update on top level document). The later is not implemented
    in 5.7 plugin.
  */

  SKIP_IF_SERVER_VERSION_LESS(8, 0, 3);

  cout << "Creating collection..." << endl;

  Schema sch = getSchema("test");
  Collection coll = sch.createCollection("c1", true);

  cout << "Adding documents..." << endl;

  coll.remove("true").execute();

  coll.add(R"({"_id":"id1", "name":"foo", "age": 1 })")
    .add(R"({"_id":"id2", "name":"bar", "age": 2 })")
    .add(R"({"_id":"id3", "name":"baz", "age": 3 })")
    .execute();

  cout << "getOne()" << endl;

  EXPECT_EQ(string("foo"), coll.getOne("id1")["name"].get<string>());
  EXPECT_EQ(string("bar"), coll.getOne("id2")["name"].get<string>());
  EXPECT_TRUE(coll.getOne("idZ").isNull());

  cout << "removeOne()" << endl;

  EXPECT_EQ(1U, coll.removeOne("id1").getAffectedItemsCount());
  EXPECT_EQ(0U, coll.removeOne("id1").getAffectedItemsCount());

  EXPECT_TRUE(coll.getOne("id1").isNull());

  cout << "replaceOne()" << endl;

  // Replace existing document
  EXPECT_EQ(1U, coll.replaceOne(
              "id3",
              expr(R"({"name": "qux", "age": cast(age+1 AS UNSIGNED INT) })"))
            .getAffectedItemsCount());
  EXPECT_EQ(string("qux"), coll.getOne("id3")["name"].get<string>());
  EXPECT_EQ(4, coll.getOne("id3")["age"].get<int>());

  cout << "replaceOne(): change id" << endl;

  // Setting a different _id on document should throw error
  // Document passed as string
  EXPECT_THROW(coll.replaceOne("id3", "{\"_id\": \"id4\", \"name\": \"baz\" }"),
               Error);
  // Document passed as a wstring
  EXPECT_THROW(coll.replaceOne("id3", L"{\"_id\": \"id4\", \"name\": \"baz\" }"),
               Error);
  // Document passed as an expression
  EXPECT_THROW(coll.replaceOne("id3", expr("{\"_id\": \"id4\", \"name\": \"baz\" }")),
               Error);
  EXPECT_THROW(coll.replaceOne("id3", expr("{\"_id\": \"id4\", \"name\": \"baz\" }")),
               Error);
  // Document passed as DbDoc
  EXPECT_THROW(coll.replaceOne("id3", DbDoc("{\"_id\": \"id4\", \"name\": \"baz\" }")),
               Error);

  cout << "getOne(): array" << endl;

  EXPECT_EQ(string("qux"), coll.getOne("id3")["name"].get<string>());
  EXPECT_EQ(string("id3"), coll.getOne("id3")["_id"].get<string>());

  cout << "replaceOne(): non-existing" << endl;

  // should affect none
  EXPECT_EQ(0U,
    coll.replaceOne("id4", expr(R"({"name": "baz" })"))
        .getAffectedItemsCount());

  cout << "Done!" << endl;
}


TEST_F(Crud, add_or_replace)
{
  SKIP_IF_NO_XPLUGIN;
  SKIP_IF_SERVER_VERSION_LESS(8, 0, 3)

  cout << "Creating collection..." << endl;

  Schema sch = getSchema("test");
  Collection coll = sch.createCollection("c1", true);

  coll.remove("true").execute();

  coll.add("{\"_id\":\"id1\", \"name\":\"foo\" }")
    .add("{\"_id\":\"id2\", \"name\":\"bar\" }")
    .add("{\"_id\":\"id3\", \"name\":\"baz\" }")
    .execute();

  cout << "Initial documents added to the collection, adding id4..." << endl;

  EXPECT_EQ(1U, coll.addOrReplaceOne("id4", "{\"name\":\"zaz\"}")
                   .getAffectedItemsCount());
  // Check that the document was added
  EXPECT_EQ(string("zaz"), coll.getOne("id4")["name"].get<string>());

  cout << "Replacing id4..." << endl;

  /*
    Note: Apparently when xplugin replaces one document with another it reports
    affected tems count as 2 - probably counting the old and the new document
    as separate.
  */

  EXPECT_LT(0U, coll.addOrReplaceOne("id4", "{\"name\":\"zzz\"}")
                   .getAffectedItemsCount());
  // Check that the document was replaced
  EXPECT_EQ(string("zzz"), coll.getOne("id4")["name"].get<string>());

  cout << "Done!" << endl;

}

TEST_F(Crud, merge_patch)
{
  SKIP_IF_NO_XPLUGIN;
  SKIP_IF_SERVER_VERSION_LESS(8, 0, 3)

  cout << "Creating collection..." << endl;

  Schema sch = getSchema("test");
  Collection coll = sch.createCollection("c1", true);

  coll.remove("true").execute();

  add_data(coll);

  coll.modify("true")
      .patch(R"({"age" : null,"birth" : { "year": year(CURDATE())-age }})")
      .execute();

  auto res = coll.find().execute();
  for (auto row : res)
  {
    EXPECT_FALSE(row.hasField("age"));
    std::cout << row["birth"]["year"] << std::endl;
  }

  coll.modify("true")
      .patch(R"({"food":["Falcoaria"], "fullname": concat("Silva", ', ', name)})")
      .execute();

  res = coll.find().execute();
  for (auto doc : res)
  {
    EXPECT_EQ(string("Falcoaria"), doc["food"][0].get<string>());
    EXPECT_THROW(doc["food"][1], std::out_of_range);
    std::cout << doc["fullname"] << std::endl;
    string fullname = string("Silva, ") + doc["name"].get<string>();
    EXPECT_EQ(fullname, doc["fullname"].get<string>());
  }

}

TEST_F(Crud, PS)
{
  SKIP_IF_NO_XPLUGIN;

  cout << "Creating collection..." << endl;

  Schema sch = getSchema("test");
  Collection coll = sch.createCollection("c1", true);

  add_data(coll);

  sql("set global max_prepared_stmt_count=199;");

  cout << "Fetching documents..." << endl;

  std::vector<CollectionFind> finds;
  std::vector<CollectionFind> finds2;
  std::vector<CollectionFind> finds3;

  auto create_find = [&finds, &coll]()
  {
    for (int i = 0; i < 100; ++i)
    {
      finds.push_back(coll.find("name like :name and age < :age"));
    }
  };

  //-1 means not set
  auto execute_find = [](std::vector<CollectionFind> &finds,int limit, int offset, unsigned expected, bool bind = true)
  {
    for (auto &find : finds)
    {
      if (limit != -1)
        find.limit(limit);
      if(offset != -1)
        find.offset(offset);

      if (bind)
        find.bind("name", "%").bind("age", 1000);

      EXPECT_EQ(expected,find.execute().count());
    }

  };

  auto execute_find_sort = [](
                           std::vector<CollectionFind> &finds,
                           bool set_sort,
                           unsigned expected)
  {
    for (auto &find : finds)
    {
      if (set_sort)
        find.sort("name DESC");


      EXPECT_EQ(expected,
                find
                .bind("name", "%")
                .bind("age", 1000)
                .execute().count());
    }

  };

  for (int i = 0; i < 2; ++i)
  {
    create_find();

    auto start_time = std::chrono::system_clock::now();

    //direct_execute
    execute_find(finds,-1, -1, 6);

    std::cout << "Direct Execute: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now()-start_time).count()
              << "(ms)" << std::endl;
    start_time = std::chrono::system_clock::now();

    //prepare+execute
    //Even if limit/offset changes, it will not fallback to the direct execute
    execute_find(finds,6,-1,6);

    std::cout << "Prepare+Execute PS: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now()-start_time).count()
              << "(ms)" << std::endl;
    start_time = std::chrono::system_clock::now();

    //execute prepared
     execute_find(finds, 6, -1, 6);

    std::cout << "Execute PS: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now()-start_time).count()
              << "(ms)" << std::endl;


    start_time = std::chrono::system_clock::now();


    //Re-use previously freed stmt_ids
    finds.clear();
    create_find();

    auto cpy_find = [&finds](std::vector<CollectionFind> &finds2)
    {
      finds2.clear();
      for(auto find : finds)
      {
        finds2.push_back(find);
      }
    };

    finds2.clear();
    finds2.clear();

    //Execute
    execute_find(finds,-1, -1, 6);
    //Prepare+Execute
    execute_find(finds,-1, -1, 6);


    cpy_find(finds2);
    cpy_find(finds3);


    //Only 100, because the PS id is shared by finds finds2 and finds3
    EXPECT_EQ(100,
              sql("select count(*) from performance_schema.prepared_statements_instances").fetchOne()[0].get<int>());

    //Since no re-prepare needed, all use same PS id

    //ExecutePrepared
    execute_find(finds,-1, -1, 6,false);
    execute_find(finds2,-1, -1, 6, false);
    execute_find(finds3,-1, -1, 6, false);

    //Only 100, because the PS id is shared by finds finds2 and finds3
    EXPECT_EQ(100,
              sql("select count(*) from performance_schema.prepared_statements_instances").fetchOne()[0].get<int>());


    //Prepare+Execute
    execute_find(finds,-1, 5, 1, false);
    execute_find(finds2,-1, 5, 1, false);
    execute_find(finds3,-1, 5, 1, false);

    //Reaches max PS because sort forces a re-prepare
    EXPECT_EQ(199,
              sql("select count(*) from performance_schema.prepared_statements_instances").fetchOne()[0].get<int>());

    //ExecutePrepared
    execute_find(finds,1, 0, 1, false);
    execute_find(finds2,1, 0, 1, false);
    execute_find(finds3,1, 0, 1, false);



    //ExecutePrepared
    execute_find(finds,1, 1, 1, false);
    execute_find(finds2,1, 1, 1, false);
    execute_find(finds3,1, 1, 1, false);
    //ExecutePrepared
    execute_find(finds,1, 1, 1, false);
    execute_find(finds2,1, 1, 1, false);
    execute_find(finds3,1, 1, 1, false);

    //SET SORT
    //Re-prepare needed, so find3 will only deirect execute because it passed
    //max_prepared_stmt_count = 200

    //Execute
    execute_find_sort(finds,true, 1);
    execute_find_sort(finds2,true, 1);
    execute_find_sort(finds3,true, 1);

    //Prepare+Execute
    execute_find_sort(finds,false, 1);

    //After release, finds take first 100 PS ids
    EXPECT_EQ(100,
              sql("select count(*) from performance_schema.prepared_statements_instances").fetchOne()[0].get<int>());

    execute_find_sort(finds2,false, 1);
    execute_find_sort(finds3,false, 1);

    //Reaches max PS, since finds no longer share ids.
    EXPECT_EQ(199,
              sql("select count(*) from performance_schema.prepared_statements_instances").fetchOne()[0].get<int>());


    //ExecutePrepared
    execute_find_sort(finds,false, 1);
    execute_find_sort(finds2,false, 1);
    execute_find_sort(finds3,false, 1);

    //clean upp the finds for next round
    finds.clear();
  }

  //Modify prepare
  {
    auto modify = coll.modify("name like :name").set("age","age+1");
    modify.bind("name","foo");
    //Execute
    modify.execute();
    //Prepare+Execute
    modify.execute();
    //ExecutePrepared
    modify.execute();
    //Execute
    modify.limit(1).execute();
    //Prepare+Execute
    modify.execute();
    //ExecutePrepared
    modify.execute();
  }

  //Remove prepare
  {
    auto modify = coll.remove("age > 10");
    //Execute
    modify.execute();
    //Prepare+Execute
    modify.execute();
    //ExecutePrepared
    modify.execute();
    //Execute
    modify.limit(1).execute();
    //Prepare+Execute
    modify.execute();
    //ExecutePrepared
    modify.execute();
  }

}

TEST_F(Crud, PS_find)
{
  SKIP_IF_NO_XPLUGIN

  auto ps_status =get_sess().sql("SELECT COUNT_EXECUTE,SQL_TEXT FROM performance_schema.prepared_statements_instances where SQL_TEXT like ?");

  auto check_ps = [&ps_status] (std::string query, int count)
  {
    ps_status.bind(std::string("%")+query+("%"));
    auto res = ps_status.execute();
    EXPECT_EQ(count != 0 ? 1U : 0U, res.count());
    for(auto row : res)
    {
      std::cout  << row[1] << std::endl;
      EXPECT_EQ(count, row[0].get<int>());
    }
  };


  Schema sch= getSchema("test");
  Collection coll= sch.createCollection("c1", true);
  coll.remove("true").execute();
  cout <<"inserting 5 documents..." <<endl;

  coll.remove("true").execute();
  {
    Result add;
    add= coll.add("{ \"name\": \"abcdefghijk\", \"age\": 1 , \"misc\": 1.2}").add("{ \"name\": \"xyz\", \"age\": 6 , \"misc\": 19.59}").execute();
    add= coll.add("{ \"name\": \"qwerty@#$%^&\", \"age\": 4 , \"misc\": 11.9}").execute();
    add= coll.add("{ \"name\": \"qwerty@#$%^&\", \"age\": 5 , \"misc\": 12.9}").execute();
    add= coll.add("{ \"name\": \"qwerty@#$%^&\", \"age\": 6 , \"misc\": 13.9}").execute();
    add= coll.add("{ \"F1\": \"@#$%^&\", \"age\": 6 , \"misc\": 13.9}").execute();
  }
  cout <<"Fetching documents..." <<endl;

  CollectionFind cFind = coll.find("name like '%@#$%'").fields("name as my_name1234567890");
  DocResult docs = cFind.execute();
  check_ps("my_name1234567890", 0);

  DbDoc doc = docs.fetchOne();
  if(!doc)
  {
    FAIL() << "Test failed : record not matching empty expression";
  }
  else
  {
    cout << " field `" << "my_name" << "`: " <<(doc)["my_name1234567890"] << endl;
  }

  docs = cFind.execute();
  check_ps("my_name1234567890", 1);

  doc = docs.fetchOne();
  if(!doc)
  {
    FAIL() << "Test failed : record not matching empty expression";
  }
  else
  {
    cout << " field `" << "my_name" << "`: " <<(doc)["my_name1234567890"] << endl;
  }

  cFind.limit(1).offset(1);
  docs = cFind.execute();
  check_ps("my_name1234567890%LIMIT ?, ?", 1);

  doc = docs.fetchOne();
  if(!doc)
  {
    cout << "Test failed : record not matching empty expression " << endl;
    FAIL() << "Test failed : record not matching empty expression";
  }
  else
  {
    cout << " field `" << "my_name" << "`: " <<(doc)["my_name1234567890"] << endl;
  }

  cFind.sort("age");
  docs = cFind.execute();
  check_ps("my_name1234567890%ORDER", 0);

  docs = cFind.execute();
  check_ps("my_name1234567890%ORDER%LIMIT ?, ?", 1);

  doc = docs.fetchOne();
  if(!doc)
  {
    cout << "Test failed : record not matching empty expression " << endl;
    FAIL() << "Test failed : record not matching empty expression";
  }
  else
  {
    cout << " field `" << "my_name" << "`: " <<(doc)["my_name1234567890"] << endl;
  }

  /* clean data */
  {
    Result add;
    add= coll.add("{ \"F1\": \"%@#$%\", \"F2\": 10 , \"ARR2\": [\"OuterData1\", \"OuterData2\", [\"InnerData1\", \"InnerData2\"], \"OuterData3\"]}").execute();
    add= coll.add("{ \"F1\": \"%@#$%\", \"F2\": 20 , \"ARR2\": [\"OuterData1\", \"OuterData2\", [\"InnerData1\", \"InnerData2\"], \"OuterData3\"]}").execute();
    add= coll.add("{ \"F1\": \"%@#$%\", \"F2\": 30 , \"ARR2\": [\"OuterData1\", \"OuterData2\", [\"InnerData1\", \"InnerData2\"], \"OuterData3\"]}").execute();
  }

  /* two limits followed by offset */
  cFind = coll.find("F1 like '%@#$%'").fields("F1 as my_old_name1234567890").limit(1);
  docs = cFind.execute();
  docs = cFind.execute();
  check_ps("my_old_name1234567890", 1);

  cFind.offset(1);
  docs = cFind.execute();
  check_ps("my_old_name1234567890%LIMIT ?, ?", 2);
  docs = cFind.execute();
  check_ps("my_old_name1234567890%LIMIT ?, ?", 3);

  /* two limits in execute */
  cFind = coll.find("F1 like '%@#$%'").fields("F1 as my_new_name1234567890");
  docs = cFind.limit(1).execute();
  check_ps("my_new_name1234567890", 0);

  docs = cFind.limit(2).execute();
  check_ps("my_new_name1234567890", 1);

  /* two fields in execute */
  cFind = coll.find("F1 like '%@#$%'");
  docs = cFind.fields("F1 as my_name1234567890").execute();
  check_ps("my_name1234567890", 0);

  docs = cFind.fields("F1 as my_name1234567890").execute();
  check_ps("my_name1234567890", 0);

  docs = cFind.execute();
  check_ps("my_name1234567890", 1);

  /* no limit only offset */
  cFind = coll.find("F1 like '%@#$%'").fields("F1 as my_name1234567890");
  cFind.offset(1);
  docs = cFind.execute();

  docs = cFind.execute();
  check_ps("my_name1234567890%LIMIT ?, ?", 1);

  /* bind with limit offset twice */
  cFind = coll.find("F1 like :F1 AND F2 > :F2").fields("F1 as my_name1234567890");
  cFind.bind("F1", "%@#$%123");
  cFind.bind("F2", 1000);
  docs = cFind.execute();

  cFind.limit(1).offset(1);
  docs = cFind.execute();
  check_ps("my_name1234567890%LIMIT ?, ?", 1);

  docs = cFind.execute();
  check_ps("my_name1234567890%LIMIT ?, ?", 2);

  /* limit in 1st stmt */
  cFind = coll.find("F1 like :F1 AND F2 > :F2").fields("F1 as my_new_name1234567890").limit(1);
  cFind.bind("F1", "%@#$%");
  cFind.bind("F2", 10);
  docs = cFind.execute();
  check_ps("my_new_name1234567890", 0);

  docs = cFind.execute();
  check_ps("my_new_name1234567890", 1);

  docs = cFind.execute();
  check_ps("my_new_name1234567890", 2);

  /* prepare, sort and limit, followed by sort */

  cFind = coll.find("F1 like :F1 AND F2 > :F2").fields("F1 as my_name_1");
  cFind.bind("F1", "%@#$%");
  cFind.bind("F2", 10);
  docs = cFind.execute();
  check_ps("my_name_1", 0);

  docs = cFind.execute();
  check_ps("my_name_1", 1);

  docs = cFind.sort("F2").limit(2).execute();
  check_ps("my_name_1%ORDER", 0);

  docs = cFind.sort("F1").execute();
  check_ps("my_name_1%ORDER", 0);

  /* sort , sort followed by limit */

  cFind = coll.find("F1 like :F1 AND F2 > :F2").fields("F1 as my_name2").limit(1);
  cFind.bind("F1", "%@#$%");
  cFind.bind("F2", 10);
  docs = cFind.execute();
  check_ps("my_name2", 0);

  docs = cFind.limit(2).execute();
  check_ps("my_name2", 1);

  docs = cFind.limit(3).offset(2).execute();
  check_ps("my_name2", 2);

  docs = cFind.limit(3).offset(3).execute();
  check_ps("my_name2", 3);

  /* execute, limit, limit offset, limit offset */

  cFind = coll.find("F1 like :F1 AND F2 > :F2").fields("F1 as my_name3");
  cFind.bind("F1", "%@#$%");
  cFind.bind("F2", 10);
  docs = cFind.execute();
  check_ps("my_name3", 0);

  docs = cFind.limit(2).execute();
  check_ps("my_name3", 1);

  docs = cFind.limit(3).offset(3).execute();
  check_ps("my_name3", 2);

  docs = cFind.limit(3).offset(2).execute();
  check_ps("my_name3", 3);

  /* sort , sort followed by limit */

  cFind = coll.find("F1 like :F1 AND F2 > :F2").fields("F1 as my_name1234567890");
  cFind.bind("F1", "%@#$%");
  cFind.bind("F2", 10);
  docs = cFind.execute();
  check_ps("my_name1234567890", 0);

  docs = cFind.sort("F2").execute();
  check_ps("my_name1234567890%ORDER", 0);

  docs = cFind.sort("F1").execute();
  check_ps("my_name1234567890%ORDER", 0);

  docs = cFind.limit(1).execute();
  check_ps("my_name1234567890%ORDER", 1);

  /* Group by */

  cFind = coll.find().fields("name as my_name_only");
  docs = cFind.execute();
  check_ps("my_name_only", 0);

  cFind.fields("max(age) as my_age", "name as my_name_only").groupBy("misc", "name").having("my_age = 5");
  docs = cFind.execute();
  check_ps("my_age", 0);
  check_ps("my_name_only", 0);

  cFind.having("my_age = 6");
  docs = cFind.execute();
  check_ps("my_age", 0);
  check_ps("my_age", 0);
  check_ps("= 6", 0);

  docs = cFind.execute();
  check_ps("my_age", 1);
  check_ps("my_age", 1);
  check_ps("= 6", 1);

  /* Check multi fields */
  cFind = coll.find("F1 like :F1 AND F2 > :F2").fields("F1 as my_F1");
  cFind.bind("F1", "%@#$%");
  cFind.bind("F2", 10);
  docs = cFind.execute();
  check_ps("my_F1", 0);

  cFind.fields("F1 as my_F1", "F2 as my_F2");
  docs = cFind.execute();
  check_ps("my_F1", 0);
  check_ps("my_F2", 0);

  docs = cFind.execute();
  check_ps("my_F1", 1);
  check_ps("my_F2", 1);
}

TEST_F(Crud, PS_modify)
{
  SKIP_IF_NO_XPLUGIN
  auto ps_status =get_sess().sql("SELECT COUNT_EXECUTE,SQL_TEXT FROM performance_schema.prepared_statements_instances where SQL_TEXT like ?");

  auto check_ps = [&ps_status] (std::string query, int count)
  {
    ps_status.bind(std::string("%")+query+("%"));
    auto res = ps_status.execute();
    EXPECT_EQ(count != 0 ? 1U : 0U, res.count());
    for(auto row : res)
    {
      std::cout  << row[1] << std::endl;
      EXPECT_EQ(count, row[0].get<int>());
    }
  };

  Schema sch= getSchema("test");
  Collection coll= sch.createCollection("c1", true);
  coll.remove("true").execute();
  cout <<"inserting 5 documents..." <<endl;
  std::string record ="{\"name\": \"New\",\"age\": 4 , ";
  std::string name1 = "Data_New";
  int klen = 64;
  int dlen = 100;

  std::string key = "";
  std::string data = "";
  key.resize(klen-1, 'S');
  data.resize(dlen-1, '$');

  record.append("\"").append(key).append("\":\"").append(data).append("\"}");
  coll.remove("true").execute();
  {
    Result add;
    add= coll.add("{ \"name\": \"abc\", \"age\": 1 , \"misc\": 1.2}").add("{ \"name\": \"xyz\", \"age\": 6 , \"misc\": 10}").execute();
    add= coll.add("{ \"name\": \"New\", \"age\": 4 , \"misc\": 3}").execute();
    add= coll.add(DbDoc(record)).execute();
  }

  Result mod1 = coll.modify("$.name like '%'").set("$.name", "Data_New").execute();
  CollectionModify cModify = coll.modify("true").set("$.name", "Data_New");

  Result mod = cModify.execute();
  check_ps("c1", 0);

  mod = cModify.execute();
  check_ps("c1", 1);

  cModify.limit(1);
  mod = cModify.execute();
  check_ps("c1%LIMIT ?", 1);

  mod = cModify.execute();
  check_ps("c1%LIMIT ?", 2);

  cModify.set("$.name", "Data_New_2");
  mod = cModify.execute();
  check_ps("Data_New_2", 0);

  mod = cModify.execute();
  check_ps("Data_New_2%LIMIT ?", 1);

  cout <<"Fetching documents..." <<endl;
  DocResult docs = coll.find("name = 'Data_New_2'").execute();
  DbDoc doc = docs.fetchOne();
  if(!doc)
  {
    FAIL() << "Test failed : record not modified by $.name ";
  }

  cModify = coll.modify("true").set("$.name", "Data_New1").set("$.age", "5").set("$.name", "Data_Newer1").unset("age");
  mod = cModify.execute();
  check_ps("Data_Newer1", 0);

  mod = cModify.execute();
  check_ps("c1", 1);
  check_ps("age", 1);
  check_ps("name", 1);
  check_ps("Data_Newer1", 1);

  cModify.limit(1);
  mod = cModify.execute();
  check_ps("c1", 1);
  check_ps("LIMIT ?", 1);

  cModify.unset("misc");
  mod = cModify.execute();
  check_ps("misc", 0);

  mod = cModify.execute();
  check_ps("misc", 1);

  cout <<"Fetching documents..." <<endl;
  docs = coll.find("name = 'Data_Newer1'").execute();
  doc = docs.fetchOne();
  if(!doc)
  {
    FAIL() << "Test failed : record not modified by $.name ";
  }


  cModify = coll.modify("name = :name").set("name", "Data_New_4").unset("name").set("name", "Data_New_5").sort("misc desc").limit(2);
  mod = cModify.bind("name", "Data_New").execute();
  check_ps("Data_New_5", 0);

  mod = cModify.bind("name", "Data_Newer1").execute();
  check_ps("Data_New_5", 1);

  cModify.limit(1);
  mod = cModify.bind("name", "Data_Newer1").execute();
  check_ps("Data_New_5", 2);

  docs = coll.find("name = 'Data_New_5' and misc = 10").execute();
  for(int i=0;i < 1;i++)
  {
    doc = docs.fetchOne();
    if(!doc)
    {
      FAIL() << "Test failed : record not modified by $.name ";
    }
  }



  /* Check arrayappend, arrayinsert, mergepatch doesn't prepare on 1st */
  coll.remove("true").execute();
  {
    Result add;
    add= coll.add("{ \"F1\": \"%@#$%\", \"ARR1\": [\"Data1\", \"Data2\", \"Data3\"] , \"ARR2\": [\"OuterData1\", \"OuterData2\", [\"InnerData1\", \"InnerData2\"], \"OuterData3\"]}").execute();
    add= coll.add("{ \"F1\": \"%@#$%\", \"ARR1\": [\"Data1\", \"Data2\", \"Data3\"] , \"ARR2\": [\"OuterData1\", \"OuterData2\", [\"InnerData1\", \"InnerData2\"], \"OuterData3\"]}").execute();
    add= coll.add("{ \"F1\": \"%@#$%\", \"ARR1\": [\"Data1\", \"Data2\", \"Data3\"] , \"ARR2\": [\"OuterData1\", \"OuterData2\", [\"InnerData1\", \"InnerData2\"], \"OuterData3\"]}").execute();
    add= coll.add(DbDoc(record)).execute();
  }

  CollectionModify cMod = coll.modify("F1 like :F1").patch("{\"ARR3\": [\"PatchedData1\", \"PatchedData2\"]}");
  mod = cMod.bind("F1", "%@#$%").execute();
  check_ps("PatchedData2", 0);

  mod = cMod.arrayAppend("$.ARR1","Data6").bind("F1", "%@#$%").execute();
  check_ps("PatchedData2", 0);
  check_ps("Data6", 0);

  mod = cMod.arrayInsert("$.ARR1[3]","NewData").bind("F1", "%@#$%").execute();
  check_ps("PatchedData2", 0);
  check_ps("Data6", 0);
  check_ps("NewData", 0);

  /* clean data */
  coll.remove("true").execute();
  {
    Result add;
    add= coll.add("{ \"F1\": \"%@#$%\", \"ARR1\": [\"Data1\", \"Data2\", \"Data3\"] , \"ARR2\": [\"OuterData1\", \"OuterData2\", [\"InnerData1\", \"InnerData2\"], \"OuterData3\"]}").execute();
    add= coll.add("{ \"F1\": \"%@#$%\", \"ARR1\": [\"Data1\", \"Data2\", \"Data3\"] , \"ARR2\": [\"OuterData1\", \"OuterData2\", [\"InnerData1\", \"InnerData2\"], \"OuterData3\"]}").execute();
    add= coll.add("{ \"F1\": \"%@#$%\", \"ARR1\": [\"Data1\", \"Data2\", \"Data3\"] , \"ARR2\": [\"OuterData1\", \"OuterData2\", [\"InnerData1\", \"InnerData2\"], \"OuterData3\"]}").execute();
    add= coll.add(DbDoc(record)).execute();
  }

  mod = cMod.patch("{\"ARR4\": [\"PatchedData3\", \"PatchedData4\"]}").bind("F1", "%@#$%").execute();
  check_ps("PatchedData2", 0);
  check_ps("PatchedData3", 0);
  check_ps("Data6", 0);
  check_ps("NewData", 0);

  /* clean data */
  coll.remove("true").execute();
  {
    Result add;
    add= coll.add("{ \"F1\": \"%@#$%\", \"ARR1\": [\"Data1\", \"Data2\", \"Data3\"] , \"ARR2\": [\"OuterData1\", \"OuterData2\", [\"InnerData1\", \"InnerData2\"], \"OuterData3\"]}").execute();
    add= coll.add("{ \"F1\": \"%@#$%\", \"ARR1\": [\"Data1\", \"Data2\", \"Data3\"] , \"ARR2\": [\"OuterData1\", \"OuterData2\", [\"InnerData1\", \"InnerData2\"], \"OuterData3\"]}").execute();
    add= coll.add("{ \"F1\": \"%@#$%\", \"ARR1\": [\"Data1\", \"Data2\", \"Data3\"] , \"ARR2\": [\"OuterData1\", \"OuterData2\", [\"InnerData1\", \"InnerData2\"], \"OuterData3\"]}").execute();
    add= coll.add(DbDoc(record)).execute();
  }

  mod = cMod.bind("F1", "%@#$%").execute();
  check_ps("PatchedData2", 1);
  check_ps("PatchedData3", 1);
  check_ps("Data6", 1);
  check_ps("NewData", 1);

  /* functions getting appended */
  coll.remove("true").execute();
  {
    Result add;
    add= coll.add("{ \"F1\": \"%@#$%\", \"ARR1\": [\"Data1\", \"Data2\", \"Data3\"] , \"ARR2\": [\"OuterData1\", \"OuterData2\", [\"InnerData1\", \"InnerData2\"], \"OuterData3\"]}").execute();
    add= coll.add("{ \"F1\": \"%@#$%\", \"ARR1\": [\"Data1\", \"Data2\", \"Data3\"] , \"ARR2\": [\"OuterData1\", \"OuterData2\", [\"InnerData1\", \"InnerData2\"], \"OuterData3\"]}").execute();
    add= coll.add("{ \"F1\": \"%@#$%\", \"ARR1\": [\"Data1\", \"Data2\", \"Data3\"] , \"ARR2\": [\"OuterData1\", \"OuterData2\", [\"InnerData1\", \"InnerData2\"], \"OuterData3\"]}").execute();
    add= coll.add(DbDoc(record)).execute();
  }

  cMod = coll.modify("F1 like :F1");

  mod = cMod.arrayInsert("$.ARR1[3]","NewNewData").bind("F1", "%@#$%").execute();
  check_ps("NewNewData", 0);

  mod = cMod.arrayInsert("$.ARR1[4]","NewNewData2").bind("F1", "%@#$%").execute();
  check_ps("NewNewData2", 0);
  check_ps("NewNewData", 0);

  mod = cMod.bind("F1", "%@#$%").execute();
  check_ps("NewNewData2", 1);
  check_ps("NewNewData2", 1);

}

TEST_F(Crud, PS_remove)
{
  SKIP_IF_NO_XPLUGIN
  auto ps_status =get_sess().sql("SELECT COUNT_EXECUTE,SQL_TEXT FROM performance_schema.prepared_statements_instances where SQL_TEXT like ?");

  auto check_ps = [&ps_status] (std::string query, int count)
  {
    ps_status.bind(std::string("%")+query+("%"));
    auto res = ps_status.execute();
    EXPECT_EQ(count != 0 ? 1U : 0U, res.count());
    for(auto row : res)
    {
      std::cout  << row[1] << std::endl;
      EXPECT_EQ(count, row[0].get<int>());
    }
  };

  Collection coll= getSchema("test").createCollection("c1", true);
  coll.remove("true").execute();
  cout <<"inserting 5 documents..." <<endl;
  std::string record ="{\"name\": \"New\",\"age\": 4 , ";
  std::string name1 = "Data_New";
  int klen = 256;
  std::string key = "";
  int dlen = 100;
  std::string data = "";

  key.resize(klen-3, 'S');
  data.resize(dlen-1, '$');
  coll.remove("true").execute();

  std::string expr = "";
  expr.append("$.").append(key).append(std::to_string(1)).append(" like ").append("'%'");
  CollectionRemove cRemove = coll.remove(expr);
  cRemove.execute();
  check_ps("c1", 0);

  for(int i=0;i<100;i++)
  {
    Result add;
    add= coll.add("{ \"name\": \"abc\", \"age\": 1 , \"misc\": 1.2}").add("{ \"name\": \"xyz\", \"age\": 6 , \"misc\": 10}").execute();
    add= coll.add("{ \"name\": \"New1\", \"age\": 4 , \"misc\": 3}").execute();
    record ="{\"name\": \"New\",\"age\": 4 , ";
    record.append("\"").append(key).append(std::to_string(i)).append("\":\"").append(data).append("\"}");

    add= coll.add(DbDoc(record)).execute();
  }


  Result mod;
  cRemove.sort("age").limit(1);
  mod = cRemove.execute();
  check_ps("c1", 0);

  mod = cRemove.execute();
  check_ps("ORDER", 1);
  check_ps("LIMIT ?", 1);

  mod = cRemove.execute();
  check_ps("c1", 2);
  check_ps("ORDER", 2);
  check_ps("LIMIT ?", 2);

  cout <<"Fetching documents..." <<endl;
  expr = "";
  expr.append(key).append(std::to_string(1)).append(" = '").append(data).append("'");
  DocResult docs = coll.find(expr).fields("count(*) as Q").execute();
  DbDoc doc = docs.fetchOne();
  if(doc == 0 || (int)doc["Q"] != 0)
  {
    FAIL() << "Test failed : record not modified by expr";
  }

  for(int i=0;i<100;i++)
  {
    Result add;
    add= coll.add("{ \"name\": \"abc\", \"age\": 1 , \"misc\": 1.2}").add("{ \"name\": \"xyz\", \"age\": 6 , \"misc\": 10}").execute();
    add= coll.add("{ \"name\": \"New1\", \"age\": 4 , \"misc\": 3}").execute();
    record ="{\"name\": \"New\",\"age\": 4 , ";
    record.append("\"").append(key).append(std::to_string(i)).append("\":\"").append(data).append("\"}");
    add= coll.add(DbDoc(record)).execute();
  }

  cRemove = coll.remove("$.name not like :name").limit(1);
  mod = cRemove.bind("name", "abc").execute();
  check_ps("not like", 0);

  mod = cRemove.bind("name", "New").execute();
  check_ps("not like", 1);

  mod = cRemove.limit(1000).bind("name", "non_existant").execute();
  check_ps("not like", 2);

  mod = cRemove.sort("age").execute();
  check_ps("ORDER%age", 0);

  mod = cRemove.sort("misc").execute();
  check_ps("ORDER%misc", 0);

  mod = cRemove.execute();
  check_ps("ORDER%misc", 1);

  cout <<"Fetching documents..." <<endl;
  // TODO: Add select criteria when myc-125 is fixed.
  expr = "";
  expr.append(key).append(std::to_string(1)).append(" = '").append(data).append("'");
  //cout << expr << endl;
  docs = coll.find(expr).fields("count(*) as Q").execute();
  doc = docs.fetchOne();
  if(doc == 0 || (int)doc["Q"] != 0)
  {
    FAIL() << "Test failed : record not modified by expr";
  }

}

TEST_F(Crud, PS_table_insert)
{
  SKIP_IF_NO_XPLUGIN

  auto ps_status =get_sess().sql("SELECT COUNT_EXECUTE,SQL_TEXT FROM performance_schema.prepared_statements_instances where SQL_TEXT like ?");

  auto check_ps = [&ps_status] (std::string query, int count)
  {
    ps_status.bind(std::string("%")+query+("%"));
    auto res = ps_status.execute();
    EXPECT_EQ(count != 0 ? 1U : 0U, res.count());
    for(auto row : res)
    {
      std::cout  << row[1] << std::endl;
      EXPECT_EQ(count, row[0].get<int>());
    }
  };

  Schema sch = getSchema("test");
  get_sess().sql("use test").execute();
  get_sess().sql("drop table if exists test.newTable").execute();
  get_sess().sql("create table test.newTable(c1 varchar(20), c2 JSON)").execute();
  Table tabNew = sch.getTable("newTable");


  /* table insert for string, dbDoc, array, need to check for map */

  TableInsert tInsert = tabNew.insert("c1", "c2").values("12345", "{ \"name\": \"abc\", \"age\": 1 , \"misc\": 1.2}");
  tInsert.execute();
  check_ps("newTable", 0);

  tInsert.execute();
  check_ps("newTable", 0);

  tInsert = tInsert.values("12345", "{ \"name\": \"abc\", \"age\": 1 , \"misc\": 1.345}");
  tInsert.execute();
  check_ps("newTable", 0);

  tInsert.execute();
  check_ps("1.345", 0);

  try{
    RowResult result = tabNew.select().where("JSON_EXTRACT(c2, \"$.age\") >= 1").execute();
    const Row r = result.fetchOne();
    cout << r[1] << r[0] << endl;
    std::string data = (string)r[0];
    if(data.compare("12345"))
    {
      FAIL() << "Mismatch in data ; " << data;
    }

  }
  catch(const char *err)
  {
    FAIL() << "Error reported : " << err;
  }

}

TEST_F(Crud, PS_table_select)
{
  SKIP_IF_NO_XPLUGIN

  auto ps_status =get_sess().sql("SELECT COUNT_EXECUTE,SQL_TEXT FROM performance_schema.prepared_statements_instances where SQL_TEXT like ?");

  auto check_ps = [&ps_status] (std::string query, int count)
  {
    ps_status.bind(std::string("%")+query+("%"));
    auto res = ps_status.execute();
    EXPECT_EQ(count != 0 ? 1U : 0U, res.count());
    for(auto row : res)
    {
      std::cout  << row[1] << std::endl;
      EXPECT_EQ(count, row[0].get<int>());
    }
  };

  Schema sch = getSchema("test");
  get_sess().sql("use test").execute();
  get_sess().sql("drop table if exists test.newTable").execute();
  get_sess().sql("create table test.newTable(c1 varchar(20), c2 JSON)").execute();
  Table tabNew = sch.getTable("newTable");


  tabNew.insert("c1", "c2").values("12345", "{ \"name\": \"abc\", \"age\": 1 , \"misc\": 1.2}").execute();
  tabNew.insert("c1", "c2").values("12345", "{ \"name\": \"abc\", \"age\": 2 , \"misc\": 1.3}").execute();
  tabNew.insert("c1", "c2").values("12345", "{ \"name\": \"abc\", \"age\": 3 , \"misc\": 1.4}").execute();

  TableSelect tSelect = tabNew.select().where("JSON_EXTRACT(c2, \"$.age\") >= 1");
  RowResult result = tSelect.execute();
  check_ps("newTable", 0);

  result = tSelect.execute();
  check_ps("newTable", 1);

  tSelect.limit(1).offset(0);
  result = tSelect.execute();
  check_ps("newTable", 1);
  check_ps("LIMIT", 1);

  tSelect.where("JSON_EXTRACT(c2, \"$.age\") >= 3");
  result = tSelect.execute();
  check_ps(">= 3", 0);

  result = tSelect.execute();
  check_ps(">= 3", 1);
  check_ps("LIMIT", 1);

  const Row r = result.fetchOne();
  cout << r[1] << r[0] << endl;
  std::string data = (string)r[0];
  if(data.compare("12345"))
  {
    FAIL() << "Mismatch in data : " << data;
  }

  tSelect.orderBy("c2->$.age");
  result = tSelect.execute();
  check_ps("ORDER", 0);

  result = tSelect.execute();
  check_ps("ORDER", 1);
  check_ps("LIMIT ?, ?", 1);

  /* Test Select with bind */
  tSelect = tabNew.select("c2->$.name as MyName").where("JSON_EXTRACT(c2, \"$.age\") >= :C2 and c1 = :C1");
  result = tSelect.bind("C2", 1).bind("C1", "12345").execute();
  check_ps("MyName", 0);

  result = tSelect.bind("C2", 1).bind("C1", "123456").execute();
  check_ps("MyName", 1);
  check_ps("SELECT", 1);
  check_ps("LIMIT ?, ?", 0);

  /* Test limit */
  tSelect.limit(1);
  result = tSelect.bind("C2", 1).bind("C1", "12345").execute();
  check_ps("MyName", 1);
  check_ps("SELECT", 1);
  check_ps("LIMIT ?, ?", 1);


  result = tSelect.bind("C2", 1).bind("C1", "123456").execute();
  check_ps("MyName", 2);
  check_ps("SELECT", 2);
  check_ps("LIMIT ?, ?", 2);


  /* Test offset */
  tSelect.offset(1);
  result = tSelect.bind("C2", 1).bind("C1", "12345").execute();
  check_ps("MyName", 3);
  check_ps("SELECT", 3);
  check_ps("LIMIT ?, ?", 3);

  /* Test sort */
  tSelect.orderBy("c2->$.age");
  result = tSelect.bind("C2", 1).bind("C1", "12345").execute();
  check_ps("ORDER", 0);

  result = tSelect.bind("C2", 1).bind("C1", "123456").execute();
  check_ps("ORDER", 1);
  check_ps("SELECT", 1);
  check_ps("LIMIT ?, ?", 1);

  tSelect.orderBy("c1");
  result = tSelect.bind("C2", 1).bind("C1", "12345").execute();
  check_ps("ORDER%c1", 0);

  result = tSelect.bind("C2", 1).bind("C1", "123456").execute();
  check_ps("ORDER%c1", 1);

  /* test multiple where */
  tSelect = tabNew.select("c2->$.name as AndNow_MyName").where("JSON_EXTRACT(c2, \"$.age\") >= :C2 and c1 = :C1");
  result = tSelect.bind("C2", 1).bind("C1", "12345").execute();
  check_ps("AndNow_MyName", 0);

  result = tSelect.where("JSON_EXTRACT(c2, \"$.age\") >= :C2 and c1 = :C1").bind("C2", 1).bind("C1", "12345").execute();
  check_ps("AndNow_MyName", 0);

  result = tSelect.limit(1).bind("C2", 1).bind("C1", "12345").execute();
  check_ps("AndNow_MyName", 1);

  /* test multiple where with diff value*/
  tSelect = tabNew.select("c2->$.name as Call").where("JSON_EXTRACT(c2, \"$.age\") >= :C2");
  result = tSelect.bind("C2", 1).execute();
  check_ps("Call", 0);

  result = tSelect.where("JSON_EXTRACT(c2, \"$.age\") >= :C2 and c1 = :C1").limit(1).bind("C2", 1).bind("C1", "12345").execute();
  check_ps("Call", 0);

  result = tSelect.limit(1).bind("C2", 1).bind("C1", "12345").execute();
  check_ps("Call", 1);
  check_ps("LIMIT ?, ?", 1);

  /* test multiple where with sort value*/
  tSelect = tabNew.select("c2->$.name as MyName").where("JSON_EXTRACT(c2, \"$.age\") >= :C2");
  result = tSelect.bind("C2", 1).execute();
  check_ps("MyName", 0);

  result = tSelect.orderBy("c1").limit(1).bind("C2", 1).execute();
  check_ps("MyName", 0);

  result = tSelect.orderBy("c2->$.age").limit(1).bind("C2", 1).execute();
  check_ps("MyName", 0);

  result = tSelect.limit(1).execute();
  check_ps("MyName", 1);
  check_ps("c2", 1);
  check_ps("ORDER", 1);
  check_ps("LIMIT ?, ?", 1);

  /* Test multiple where */
  tSelect = tabNew.select("c2->$.name as ShouldBeIt").where("JSON_EXTRACT(c2, \"$.age\") >= 1");
  result = tSelect.execute();
  check_ps("ShouldBeIt", 0);

  result = tSelect.execute();
  check_ps("ShouldBeIt", 1);
  check_ps("age", 1);
  check_ps(">= 1", 1);

  tSelect.where("JSON_EXTRACT(c2, \"$.misc\") > 1");
  result = tSelect.execute();
  check_ps("misc", 0);

  result = tSelect.execute();
  check_ps("misc", 1);

}

TEST_F(Crud, PS_table_update)
{
  SKIP_IF_NO_XPLUGIN

  auto ps_status =get_sess().sql("SELECT COUNT_EXECUTE,SQL_TEXT FROM performance_schema.prepared_statements_instances where SQL_TEXT like ?");

  auto check_ps = [&ps_status] (std::string query, int count)
  {
    ps_status.bind(std::string("%")+query+("%"));
    auto res = ps_status.execute();
    EXPECT_EQ(count != 0 ? 1U : 0U, res.count());
    for(auto row : res)
    {
      std::cout  << row[1] << std::endl;
      EXPECT_EQ(count, row[0].get<int>());
    }
  };

  Schema sch =getSchema("test");
  get_sess().sql("use test").execute();
  get_sess().sql("drop table if exists test.newTable").execute();
  get_sess().sql("create table test.newTable(c1 varchar(20), c2 JSON, c3 long, c4 double, c5 date, c6 JSON)").execute();
  Table tabNew = sch.getTable("newTable");


  /* table insert for string, dbDoc, array, need to check for map */

  tabNew.insert("c1", "c2", "c3", "c4", "c5").values("123451", "{ \"name\": \"abc1\", \"age\": 9223372036854775800 , \"misc\": 222222222221.333333333333}", 222222222222221, 222222222221.333333333333, "2012-06-18").execute();
  tabNew.insert("c1", "c2", "c3", "c4", "c5").values("123452", "{ \"name\": \"abc2\", \"age\": 9223372036854775801 , \"misc\": 222222222222.333333333333}", 222222222222222, 222222222222.333333333333, "2012-06-19").execute();
  tabNew.insert("c1", "c2", "c3", "c4", "c5").values("123453", "{ \"name\": \"abc3\", \"age\": 9223372036854775802 , \"misc\": 222222222223.333333333333}", 222222222222223, 222222222223.333333333333, "2012-06-20").execute();
  tabNew.insert("c1", "c2", "c3", "c4", "c5").values("123454", "{ \"name\": \"abc4\", \"age\": 9223372036854775803 , \"misc\": 222222222224.11}", 222222222222224, 222222222224.333333333333, "2012-06-21").execute();
  tabNew.insert("c1", "c2", "c3", "c4", "c5", "c6").values("123456", "{ \"name\": \"abc5\", \"age\": 9223372036854775804 , \"misc\": 222222222225.333333333333}", 222222222222225, 222222222225.333333333333, "2012-06-22", "{ \"name\": [\"abc5\",\"abc6\", \"abc7\"], \"age\": 9223372036854775804 , \"misc\": 222222222225.333333333333}").execute();


  RowResult result = tabNew.select().where("9223372036854775801 in c2->$.age").execute();
  const Row r = result.fetchOne();
  cout << r[1] << r[0] << endl;
  std::string data = (string)r[0];
  if(data.compare("123452"))
  {
    FAIL() << "Mismatch in data : " << data;
  }

  {
    TableUpdate tUpdate = tabNew.update().set("c2->$.name", expr("concat(c2->$.name, '-updated')")).where("'abc4' in c2->$.name");
    tUpdate.execute();
    check_ps("newTable", 0);

    tUpdate.execute();
    check_ps("newTable", 1);
    check_ps("-updated", 1);
    check_ps("abc4", 1);
    check_ps("name", 1);

    tUpdate.limit(1);
    tUpdate.execute();
    check_ps("newTable", 1);
    check_ps("LIMIT", 1);
    check_ps("-updated", 1);
    check_ps("abc4", 1);
    check_ps("name", 1);

    tUpdate.limit(3).execute();
    check_ps("newTable", 2);
    check_ps("LIMIT", 2);
    check_ps("-updated", 2);
    check_ps("abc4", 2);
    check_ps("name", 2);

    tUpdate.where("'abc2' in c2->$.name");
    tUpdate.execute();
    check_ps("abc2", 0);

    tUpdate.execute();
    check_ps("LIMIT", 1);
    check_ps("abc2", 1);
    check_ps("-updated", 1);
    check_ps("name", 1);

    RowResult result = tabNew.select().where("'abc2-updated' in c2->$.name").execute();
    const Row r = result.fetchOne();
    cout << r[1] << r[0] << endl;
    std::string data = (string)r[0];
    if(data.compare("123452"))
    {
      FAIL() << "Mismatch in data : " << data.c_str();
    }

  }


  {
    TableUpdate tUpdate = tabNew.update().set("c2->$.name", expr("concat(c2->$.name, '-updated')")).where("c6->$.name[0] = :name");
    tUpdate.bind("name", "abc1").execute();
    check_ps("c6", 0);

    tUpdate.bind("name", "abc5").execute();
    check_ps("c6", 1);

    RowResult result = tabNew.select().where("'abc5-updated' = c2->$.name").execute();
    const Row r = result.fetchOne();
    cout << r[1] << r[0] << endl;
    std::string data = (string)r[0];
    if(data.compare("123456"))
    {
      FAIL() <<  "Mismatch in data : " << data;
    }
  }


  /* remove all and insert */
  tabNew.remove().where("true").execute();
  tabNew.insert("c1", "c2").values("12345", "{ \"name\": \"abc\", \"age\": 1 , \"misc\": 1.2}").execute();
  tabNew.insert("c1", "c2").values("123456", "{ \"name\": \"abc\", \"age\": 2 , \"misc\": 1.3}").execute();
  tabNew.insert("c1", "c2").values("1234567", "{ \"name\": \"abc\", \"age\": 3 , \"misc\": 1.4}").execute();

  /* Test update with bind */
  TableUpdate tUpdate = tabNew.update().set("c1", expr("concat(c1, '-updated')")).where("c2->$.name like :C2 and :C1 = c1");
  tUpdate.bind("C1", "123456");
  tUpdate.bind("C2", "abc");
  tUpdate.execute();
  check_ps("c1", 0);

  tUpdate.bind("C1", "12345");
  tUpdate.bind("C2", "abc");
  tUpdate.execute();
  check_ps("c1", 1);
  check_ps("updated", 1);
  check_ps("c2", 1);
  check_ps("ORDER BY", 0);

  /* Test with limit */
  tUpdate.limit(1);
  tUpdate.bind("C1", "1234567");
  tUpdate.bind("C2", "abc");
  tUpdate.execute();
  check_ps("newTable", 1);
  check_ps("updated", 1);
  check_ps("LIMIT", 1);
  check_ps("c2", 1);
  check_ps("ORDER BY", 0);

  /* remove all and insert */
  tabNew.remove().where("true").execute();
  tabNew.insert("c1", "c2").values("12345", "{ \"name\": \"abc\", \"age\": 1 , \"misc\": 1.2}").execute();
  tabNew.insert("c1", "c2").values("123456", "{ \"name\": \"abc\", \"age\": 2 , \"misc\": 1.3}").execute();
  tabNew.insert("c1", "c2").values("1234567", "{ \"name\": \"abc\", \"age\": 3 , \"misc\": 1.4}").execute();

  tUpdate.limit(2);
  tUpdate.bind("C1", "1234567");
  tUpdate.bind("C2", "abc");
  tUpdate.execute();
  check_ps("newTable", 2);
  check_ps("updated", 2);
  check_ps("LIMIT", 2);
  check_ps("c2", 2);
  check_ps("ORDER BY", 0);
  check_ps("LIMIT ?, ?", 0);

  /* Test with sort */
  tUpdate.orderBy("c1");
  tUpdate.bind("C1", "12345");
  tUpdate.bind("C2", "abc");
  tUpdate.execute();
  check_ps("ORDER BY", 0);


  tUpdate.bind("C1", "123456");
  tUpdate.bind("C2", "abc");
  tUpdate.execute();
  check_ps("newTable", 1);
  check_ps("updated", 1);
  check_ps("LIMIT", 1);
  check_ps("c2", 1);
  check_ps("ORDER BY", 1);


  /* remove all and insert */
  tabNew.remove().where("true").execute();
  tabNew.insert("c1", "c2").values("12345", "{ \"name\": \"abc\", \"age\": 1 , \"misc\": 1.2}").execute();
  tabNew.insert("c1", "c2").values("123456", "{ \"name\": \"abc\", \"age\": 2 , \"misc\": 1.3}").execute();
  tabNew.insert("c1", "c2").values("1234567", "{ \"name\": \"abc\", \"age\": 3 , \"misc\": 1.4}").execute();

  tUpdate.orderBy("c2->$.age");
  tUpdate.bind("C1", "12345");
  tUpdate.bind("C2", "abc");
  tUpdate.execute();
  check_ps("$.age", 0);

  /* check set for update */
  tUpdate = tabNew.update().set("c1", expr("concat(c1, '-outdated')")).where("c2->$.name like :C2 and :C1 = c1");
  tUpdate.bind("C1", "123456");
  tUpdate.bind("C2", "abc");
  tUpdate.execute();
  check_ps("-outdated", 0);

  tUpdate.set("c1", expr("concat(c1, '-check')")).limit(1).execute();
  check_ps("-check", 0);

  tUpdate.execute();
  check_ps("-check", 1);

  /* check set for update */
  tUpdate = tabNew.update().where("c2->$.name like :C2 and :C1 = c1");
  tUpdate.bind("C1", "123456");
  tUpdate.bind("C2", "abc");
  tUpdate.set("c1", expr("concat(c1, '-updated')")).execute();
  check_ps("-updated", 0);


  tUpdate.set("c2", "{ \"name\": \"abcNew\", \"age\": 3 , \"misc\": 1.4}").limit(1).execute();
  check_ps("abcNew", 0);

  tUpdate.execute();
  check_ps("abcNew", 1);

}

TEST_F(Crud, PS_table_delete)
{
  SKIP_IF_NO_XPLUGIN

  auto ps_status =get_sess().sql("SELECT COUNT_EXECUTE,SQL_TEXT FROM performance_schema.prepared_statements_instances where SQL_TEXT like ?");

  auto check_ps = [&ps_status] (std::string query, int count)
  {
    ps_status.bind(std::string("%")+query+("%"));
    auto res = ps_status.execute();
    EXPECT_EQ(count != 0 ? 1U : 0U, res.count());
    for(auto row : res)
    {
      std::cout  << row[1] << std::endl;
      EXPECT_EQ(count, row[0].get<int>());
    }
  };

  /* ddl */
  std::string strValue = "";
  get_sess().sql("use test").execute();
  get_sess().sql("drop table if exists newtable").execute();
  get_sess().sql("create table newtable(f0 int, f1 varchar(20))").execute();

  strValue = "abcdef";
  auto sBindSQL = get_sess().sql("insert into newtable values(?, ?)");
  auto sSQL = get_sess().sql("insert into newtable values(33, 'abcdef')");
  sSQL.execute();
  check_ps("newTable", 0);
  sBindSQL.bind(255).bind("NewValue").execute();
  check_ps("newTable", 0);

  for(int i=0;i<100;i++)
  {
    sSQL.execute();
    check_ps("newtable", 0);
    sBindSQL.bind(255+i).bind("NewValue_1").execute();
    check_ps("newtable", 0);
  }

  sSQL = get_sess().sql("select f0, f1 from newtable where f0 <= 33");
  RowResult res = sSQL.execute();
  check_ps("newtable", 0);

  res = sSQL.execute();
  check_ps("newtable", 0);

  Row row = res.fetchOne();

  //if(row == 0 || (int)row[0]!=33 || string("abcdef").compare((string)row[0]))
  if(row == 0 )
  {
    FAIL() << "Fetch fail";
  }

  string data = row[1];
  int num = row[0];

  cout << data << num << endl;

}


TEST_F(Crud, PS_max)
{
  SKIP_IF_NO_XPLUGIN

  auto ps_status =get_sess().sql("SELECT COUNT_EXECUTE,SQL_TEXT FROM performance_schema.prepared_statements_instances where SQL_TEXT like ?");

  auto check_ps = [&ps_status] (std::string query, int count)
  {
    ps_status.bind(std::string("%")+query+("%"));
    auto res = ps_status.execute();
    EXPECT_EQ(count != 0 ? 1U : 0U, res.count());
    for(auto row : res)
    {
      std::cout  << row[1] << std::endl;
      EXPECT_EQ(count, row[0].get<int>());
    }
  };

  Schema sch= getSchema("test");
  Collection coll= sch.createCollection("c1", true);
  coll.remove("true").execute();
  cout <<"inserting 5 documents..." <<endl;

  coll.remove("true").execute();
  {
    Result add;
    add= coll.add("{ \"name\": \"abcdefghijk\", \"age\": 1 , \"misc\": 1.2}").add("{ \"name\": \"xyz\", \"age\": 6 , \"misc\": 19.59}").execute();
    add= coll.add("{ \"name\": \"qwerty@#$%^&\", \"age\": 4 , \"misc\": 11.9}").execute();
    add= coll.add("{ \"name\": \"qwerty@#$%^&\", \"age\": 5 , \"misc\": 12.9}").execute();
    add= coll.add("{ \"name\": \"qwerty@#$%^&\", \"age\": 6 , \"misc\": 13.9}").execute();
  }
  cout <<"Fetching documents..." <<endl;

  std::list <CollectionFind> cFind;
  std::string strField;

  for (int i=1; i<=1000; ++i)
  {
    strField = "name as my_name"+std::to_string(i);
    cFind.push_back(coll.find("name like '%@#$%'").fields(strField));
  }

  DocResult docs;
  DbDoc doc;

  int i=0;
  for(const auto& cFindMem : cFind)
  {
    CollectionFind cFindOne = cFindMem;
    strField = "my_name"+std::to_string(i+1);
    docs = cFindOne.execute();
    check_ps(strField, 0);
    docs = cFindOne.execute();
    check_ps(strField, 1);
    doc = docs.fetchOne();
    if(doc == 0)
    {
      FAIL() << "Test failed : record not matching empty expression";
    }
    else
    {
      cout << " field `" << strField << "`: " <<(doc)[strField.c_str()] << endl;
    }

    cFindOne.fields("name as my_name_is_now_new");
    docs = cFindOne.execute();
    check_ps("my_name_is_now_new", 0);

    docs = cFindOne.execute();
    check_ps("my_name_is_now_new", 1);

    i++;
  }

  check_ps(strField, 0);

}

TEST_F(Crud, PS_SQL)
{
  SKIP_IF_NO_XPLUGIN

  auto ps_status =get_sess().sql("SELECT COUNT_EXECUTE,SQL_TEXT FROM performance_schema.prepared_statements_instances where SQL_TEXT like ?");

  auto check_ps = [&ps_status] (std::string query, int count)
  {
    ps_status.bind(std::string("%")+query+("%"));
    auto res = ps_status.execute();
    EXPECT_EQ(count != 0 ? 1U : 0U, res.count());
    for(auto row : res)
    {
      std::cout  << row[1] << std::endl;
      EXPECT_EQ(count, row[0].get<int>());
    }
  };

  /* ddl */
  std::string strValue = "";

  get_sess().sql("use test").execute();
  get_sess().sql("drop table if exists newtable").execute();
  get_sess().sql("create table newtable(f0 int, f1 varchar(20))").execute();

  strValue = "abcdef";
  auto sBindSQL = get_sess().sql("insert into newtable values(?, ?)");
  auto sSQL = get_sess().sql("insert into newtable values(33, 'abcdef')");
  sSQL.execute();
  check_ps("newTable", 0);
  sBindSQL.bind(255).bind("NewValue").execute();
  check_ps("newTable", 0);

  for(int i=0;i<100;i++)
  {
    sSQL.execute();
    check_ps("newtable", 0);
    sBindSQL.bind(255+i).bind("NewValue_1").execute();
    check_ps("newtable", 0);
  }

  sSQL = get_sess().sql("select f0, f1 from newtable where f0 <= 33");
  RowResult res = sSQL.execute();
  check_ps("newtable", 0);

  res = sSQL.execute();
  check_ps("newtable", 0);

  Row row = res.fetchOne();

  //if(row == 0 || (int)row[0]!=33 || string("abcdef").compare((string)row[0]))
  if(row == 0 )
  {
    FAIL() << "Fetch fail";
  }

  string data = row[1];
  int num = row[0];

  cout << data << num << endl;

}



TEST_F(Crud, overlaps)
{
  SKIP_IF_NO_XPLUGIN;
  SKIP_IF_SERVER_VERSION_LESS(8, 0, 15)

  cout << "Creating collection..." << endl;

  Schema sch = getSchema("test");
  Collection coll = sch.createCollection("c1", true);

  coll.remove("true").execute();


  coll.add("{ \"name\": \"foo\", \"age\": 2, \
           \"food\": [\"Milk\", \"Soup\"] }")
      .add("{ \"name\": \"baz\", \"age\": 2, \
           \"food\": [\"Beer\", \"Soup\"] }")
      .execute();

  auto res = coll.find(R"(food overlaps ["Soup"])").execute();
  EXPECT_EQ(2U, res.count());

  res = coll.find(R"(food overlaps ["Milk", "Soup"])").execute();
  EXPECT_EQ(2U, res.count());

  res = coll.find(R"(food overlaps ["Milk"])").execute();
  EXPECT_EQ(1U, res.count());
  EXPECT_EQ(string("foo"),res.fetchOne()["name"]);

  res = coll.find(R"(food overlaps ["Beer"])").execute();
  EXPECT_EQ(1U, res.count());
  EXPECT_EQ(string("baz"),res.fetchOne()["name"]);

  res = coll.find(R"(food overlaps ["Meat"])").execute();
  EXPECT_EQ(0U, res.count());

  res = coll.find(R"(food overlaps "Meat")").execute();
  EXPECT_EQ(0U, res.count());

  // Not Overlaps tests

  res = coll.find(R"(food not overlaps ["Soup"])").execute();
  EXPECT_EQ(0U, res.count());

  res = coll.find(R"(food not overlaps ["Milk", "Soup"])").execute();
  EXPECT_EQ(0U, res.count());

  res = coll.find(R"(food not overlaps ["Milk"])").execute();
  EXPECT_EQ(1U, res.count());
  EXPECT_EQ(string("baz"),res.fetchOne()["name"]);

  res = coll.find(R"(food not overlaps ["Beer"])").execute();
  EXPECT_EQ(1U, res.count());
  EXPECT_EQ(string("foo"),res.fetchOne()["name"]);

  res = coll.find(R"(food not overlaps ["Meat"])").execute();
  EXPECT_EQ(2U, res.count());

  res = coll.find(R"(food not overlaps "Meat")").execute();
  EXPECT_EQ(2U, res.count());

  try {
    coll.find(R"(food not overlaps and "Meat")").execute();
    FAIL() << "No error thrown";
  } catch (Error& e) {
    std::cout << "Expected: " << e << std::endl;
  }

  try {
    coll.find(R"(food and overlaps "Meat")").execute();
    FAIL() << "No error thrown";
  } catch (Error& e) {
    std::cout << "Expected: " << e << std::endl;
  }

}


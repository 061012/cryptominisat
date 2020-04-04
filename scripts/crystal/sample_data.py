#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Copyright (C) 2017  Mate Soos
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; version 2
# of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.

from __future__ import print_function
import sqlite3
import optparse
import time
import os.path
import helper


class QueryDatRem(helper.QueryHelper):
    def __init__(self, dbfname):
        super(QueryDatRem, self).__init__(dbfname)

    def dangerous(self):
        self.c.execute("PRAGMA journal_mode = MEMORY")
        self.c.execute("PRAGMA synchronous = OFF")
        pass


    def create_indexes1(self):
        print("Recreating indexes...")
        t = time.time()
        queries = """
        create index `idxclid31` on `clause_stats` (`clauseID`);
        create index `idxclid32` on `reduceDB` (`clauseID`);
        create index `idxclid33` on `sum_cl_use` (`clauseID`);
        create index `idxclid34` on `used_clauses` (`clauseID`);
        create index `idxclid44` on `restart_dat_for_cl` (`clauseID`);
        create index `idxclid35` on `var_data_fintime` (`var`, `sumConflicts_at_picktime`);
        create index `idxclid36` on `var_data_picktime` (`var`, `sumConflicts_at_picktime`);
        create index `idxclid37` on `dec_var_clid` (`var`, `sumConflicts_at_picktime`);
        create index `idxclid40` on `restart_dat_for_var` (`conflicts`);
        """

        for q in queries.split("\n"):
            self.c.execute(q)

        print("Created indexes needed T: %-3.2f s"% (time.time() - t))

    def recreate_used_ID_table(self):
        q = """
        DROP TABLE IF EXISTS `used_cl_ids`;
        """
        self.c.execute(q)

        q = """
        CREATE TABLE `used_cl_ids` (
          `clauseID` int(20) NOT NULL
        );
        """
        self.c.execute(q)

        q = """
        create index `idxclid30` on `used_cl_ids` (`clauseID`);
        """
        self.c.execute(q)

    def remove_too_many_vardata(self):
        t = time.time()
        q = """
        select count()
        from var_data_picktime
        """
        ret = self.c.execute(q)
        rows = self.c.fetchall()
        assert len(rows) == 1
        num_vardata = rows[0][0]
        print("Current number of elements in var_data: %d" % num_vardata)

        if num_vardata < options.goal_vardata:
            print("Not too many in var_data, skipping removal.")
            return

        q = """
        DROP TABLE IF EXISTS `used_vardat`;
        """
        self.c.execute(q)

        q = """
        CREATE TABLE `used_vardat` (
          `var` bigint(20) NOT NULL
          , `sumConflicts_at_picktime` bigint(20) NOT NULL
        );
        """
        self.c.execute(q)

        q = """
        create index `idxclidxx` on `used_vardat`
        (`var`, `sumConflicts_at_picktime`);
        """
        self.c.execute(q)

        q = """
        insert into `used_vardat`
        SELECT
        var, sumConflicts_at_picktime
        FROM var_data_picktime
        order by random()
        limit {limit}
        """.format(limit=options.goal_vardata)
        self.c.execute(q)
        print("Added {limit} to `used_vardat`".format(limit=options.goal_vardata))
        print("--> T: %-3.2f s"% (time.time() - t))

        t = time.time()
        del_from = ["var_data_picktime", "var_data_fintime", "dec_var_clid"]
        for table in del_from:

            q = """
            DROP TABLE IF EXISTS `myrows`;
            """
            self.c.execute(q)

            q = """
            CREATE TABLE `myrows` (
              `myrowid` bigint(20) NOT NULL
            );
            """
            self.c.execute(q)

            q = """
            INSERT INTO myrows
            SELECT `rowid`
            FROM `{table}` WHERE (`var`, sumConflicts_at_picktime)
            in (SELECT `var`, sumConflicts_at_picktime from used_vardat);
            """
            self.c.execute(q.format(table=table))

            q = """
            create index `myidx111` on `myrows` (`myrowid`);
            """

            q = """
            DELETE FROM `{table}` WHERE (rowid) NOT IN
            (SELECT `myrowid` from `myrows` );"""
            self.c.execute(q.format(table=table))
            print("Deleted unused data from %s" % table)

        # cleanup
        q = """
        DROP TABLE IF EXISTS `myrows`;
        """
        self.c.execute(q)

        # sample restart_dat_for_var
        q = """
        DELETE FROM restart_dat_for_var WHERE `conflicts` NOT IN
        (SELECT `sumConflicts_at_picktime` from `used_vardat` group by sumConflicts_at_picktime);"""
        self.c.execute(q)
        print("Deleted unused data from restart_dat_for_var")

        # cleanup
        q = """
        DROP TABLE IF EXISTS `used_vardat`;
        """
        self.c.execute(q)
        print("Cleaned up var_data_x & restart_dat_for_var tables T: %-3.2f s"
              % (time.time() - t))

    def insert_into_used_cls_ids_from_clstats(self, min_used, limit, max_used=None):
        min_used = int(min_used)

        max_const = ""
        if max_used is not None:
            max_const = " and num_used <= %d" % max_used

        t = time.time()
        val = int()
        q = """
        insert into used_cl_ids
        select
        clauseID from sum_cl_use
        where
        num_used >= {min_used}
        {max_const}
        order by random() limit {limit}
        """.format(
            min_used=min_used,
            limit=int(limit),
            max_const=max_const)


        self.c.execute(q)
        print("Added num_used >= %d from sum_cl_use to used_cls_ids T: %-3.2f s"
              % (min_used, time.time() - t))

    def insert_into_used_cls_ids_from_clstats_from_rdb(self, limit):
        limit = int(limit)

        t = time.time()
        val = int()
        q = """
        insert into used_cl_ids
        select
        clauseID from reduceDB
        where
        is_ternary_resol_cl = 1
        group by clauseID
        order by random() limit {limit}
        """.format(limit=int(limit))


        self.c.execute(q)
        print("Added clauseIDs from reduceDB that are TERNARY from reduceDB to used_cls_ids T: %-3.2f s"
              % (time.time() - t))

    # inserts less than 1-1 ratio, inserting only 0.3*N from unused ones
    def fill_used_cl_ids_table(self):
        t = time.time()
        if not options.fair:
            self.insert_into_used_cls_ids_from_clstats(min_used=30, limit=options.limit/5)
            self.insert_into_used_cls_ids_from_clstats(min_used=20, limit=options.limit/4)
            self.insert_into_used_cls_ids_from_clstats(min_used=5, limit=options.limit/3)
            self.insert_into_used_cls_ids_from_clstats(min_used=1, limit=options.limit/2)

        self.insert_into_used_cls_ids_from_clstats_from_rdb(limit=options.limit/2)
        self.insert_into_used_cls_ids_from_clstats(min_used=0, limit=options.limit, max_used=0)

        q = """
        select count()
        from used_cl_ids, sum_cl_use
        where
        used_cl_ids.clauseID = sum_cl_use.clauseID
        and sum_cl_use.num_used > 0
        """
        ret = self.c.execute(q)
        rows = self.c.fetchall()
        assert len(rows) == 1
        good_ids = rows[0][0]

        q = """
        select count()
        from used_cl_ids, sum_cl_use
        where
        used_cl_ids.clauseID = sum_cl_use.clauseID
        and sum_cl_use.num_used = 0
        """
        ret = self.c.execute(q)
        rows = self.c.fetchall()
        assert len(rows) == 1
        bad_ids = rows[0][0]

        print("IDs in used_cl_ids that are 'good' (sum_cl_use.num_used > 0) : %d" % good_ids)
        print("IDs in used_cl_ids that are 'bad'  (sum_cl_use.num_used = 0) : %d" % bad_ids)
        print("   T: %-3.2f s" % (time.time() - t))

    def print_idxs(self):
        print("Using indexes: ")
        q = """
        SELECT * FROM sqlite_master WHERE type == 'index'
        """
        self.c.execute(q)
        rows = self.c.fetchall()
        queries = ""
        for row in rows:
            print("-> index:", row)

    def filter_tables_of_ids(self):
        self.print_idxs()

        tables = ["clause_stats", "reduceDB", "sum_cl_use", "used_clauses", "restart_dat_for_cl", "cl_last_in_solver"]
        q = """
        DELETE FROM {table} WHERE clauseID NOT IN
        (SELECT clauseID from used_cl_ids );"""

        for table in tables:
            t = time.time()
            self.c.execute(q.format(table=table))
            print("Filtered table '%s' T: %-3.2f s" % (table, time.time() - t))

    def print_sum_cl_use_distrib(self):
        q = """
        select c, num_used from (
        select count(*) as c, num_used
        from sum_cl_use
        group by num_used) order by num_used
        """
        self.c.execute(q)
        rows = self.c.fetchall()
        print("Distribution of clause uses:")
        total = 0
        zero_use = 0
        for i in range(len(rows)):
            cnt = int(rows[i][0])
            numuse = int(rows[i][1])
            if numuse == 0:
                zero_use = cnt
            total += cnt

        i = 0
        while i < len(rows):
            cnt = int(rows[i][0])
            numuse = int(rows[i][1])

            this_cnt_tot = 0
            this_numuse_tot = 0
            for x in range(100000):
                if i+x >= len(rows):
                    i+=x
                    break

                this_cnt = int(rows[i+x][0])
                this_numuse = int(rows[i+x][1])
                this_cnt_tot += this_cnt
                this_numuse_tot += this_numuse
                if this_cnt_tot > 300:
                    i+=x
                    i+=1
                    break
            print("  ->  {cnt:-8d} of sum_cl_use: {numuse:-7d}-{this_numuse:-7d}  --  {percent:-3.5f} ratio".format(
                    cnt=this_cnt_tot, numuse=numuse, this_numuse=this_numuse, percent=(this_cnt_tot/total)))


        print("Total: %d of which zero_use: %d" % (total, zero_use))
        if zero_use == 0 or zero_use/total < 0.1:
            print("ERROR: Zero use is very low, this is almost surely a bug!")
            exit(-1)

    def check_db_sanity(self):
        print("Checking tables in DB...")
        q = """
        SELECT name FROM sqlite_master WHERE type == 'table'
        """
        found_sum_cl_use = False
        self.c.execute(q)
        rows = self.c.fetchall()
        for row in rows:
            if row[0] == "sum_cl_use":
                found_sum_cl_use = True

            print("-> We have table: ", row[0])
            if row[0] == "used_later_short" or row[0] == "used_later_long":
                print("ERROR: 'gen_pandas.py' has been already ran on this DB")
                print("       this will be a mess. We cannot run. ")
                print("       Exiting.")
                exit(-1)

        if not found_sum_cl_use:
            print("ERROR: Did not find sum_cl_use table. You probably didn't run")
            print("       the 'clean_update_data.py' on this database")
            print("       Exiting.")
            exit(-1)

        q = """
        SELECT count() FROM sum_cl_use where num_used = 0
        """
        self.c.execute(q)
        rows = self.c.fetchall()
        assert len(rows) == 1
        num = int(rows[0][0])
        print("Unused clauses in sum_cl_use: ", num)
        if num == 0:
            print("ERROR: You most likely didn't run 'clean_data.py' on this database")
            print("       Exiting.")
            exit(-1)

        print("Tables seem OK")

    def create_indexes2(self):
        helper.drop_idxs(self.c)

        print("Recreating indexes...")
        t = time.time()
        queries = """
        create index `idxclid6-4` on `reduceDB` (`clauseID`, `conflicts`);
        create index `idxclidUCLS-1` on `used_clauses` ( `clauseID`, `used_at`);
        """
        for q in queries.split('\n'):
            t2 = time.time()

            if options.verbose:
                print("Creating index: ", q)
            self.c.execute(q)
            if options.verbose:
                print("Index creation T: %-3.2f s" % (time.time() - t2))

        print("Indexes created T: %-3.2f s" % (time.time() - t))

    def fill_later_useful_data(self):
        t = time.time()
        q = """
        DROP TABLE IF EXISTS `used_later`;
        """
        for l in q.split('\n'):
            self.c.execute(l)
        print("used_later dropped T: %-3.2f s" % (time.time() - t))

        self.print_idxs()

        q = """
        create table `used_later` (
            `clauseID` bigint(20) NOT NULL,
            `rdb0conflicts` bigint(20) NOT NULL,
            `used_later` bigint(20)
        );"""
        self.c.execute(q)
        print("used_later recreated T: %-3.2f s" % (time.time() - t))

        t = time.time()
        q = """insert into used_later
        (
        `clauseID`,
        `rdb0conflicts`,
        `used_later`
        )
        SELECT
        rdb.clauseID
        , rdb.conflicts
        , count(ucl.used_at) as `useful_later`
        FROM
        reduceDB as rdb
        left join used_clauses as ucl

        -- for any point later than now
        on (ucl.clauseID = rdb.clauseID
            and ucl.used_at > rdb.conflicts)

        WHERE
        rdb.clauseID != 0

        group by rdb.clauseID, rdb.conflicts;"""
        self.c.execute(q)
        print("used_later filled T: %-3.2f s" % (time.time() - t))

        t = time.time()
        q = """
        create index `used_later_idx1` on `used_later` (`clauseID`, rdb0conflicts);
        create index `used_later_idx2` on `used_later` (`clauseID`, rdb0conflicts, used_later);
        """
        for l in q.split('\n'):
            self.c.execute(l)
        print("used_later indexes added T: %-3.2f s" % (time.time() - t))


    def insert_into_only_keep_rdb(self, min_used_later, limit):
        limit = int(limit)
        t = time.time()
        q = """
        insert into only_keep_rdb (id)
        select
        rdb0.rowid
        from reduceDB as rdb0, used_later
        where
        used_later.clauseID=rdb0.clauseID
        and used_later.rdb0conflicts=rdb0.conflicts
        and used_later.used_later >= {min_used_later}
        order by random()
        limit {limit}""".format(min_used_later=min_used_later, limit=limit)
        self.c.execute(q)
        print("Insert only_keep_rdb where used_later >= %d T: %-3.2f s" %
              (min_used_later, time.time() - t))

    def delete_too_many_rdb_rows(self):
        t = time.time()
        val = int(options.limit)
        ret = self.c.execute("select count() from reduceDB")
        rows = self.c.fetchall()
        rdb_rows = rows[0][0]
        print("Have %d lines of RDB" % (rdb_rows))

        q = """
        drop table if exists only_keep_rdb;
        """
        self.c.execute(q)

        t = time.time()
        q = """create table only_keep_rdb (
            id bigint(20) not null
        );"""
        self.c.execute(q)
        print("Created only_keep_rdb T: %-3.2f s" % (time.time() - t))

        if not options.fair:
            self.insert_into_only_keep_rdb(20, options.goal_rdb/5)
            self.insert_into_only_keep_rdb(10, options.goal_rdb/5)
            self.insert_into_only_keep_rdb(5, options.goal_rdb/3)
            self.insert_into_only_keep_rdb(1, options.goal_rdb/2)

        self.insert_into_only_keep_rdb(0, options.goal_rdb)

        t = time.time()
        ret = self.c.execute("select count() from only_keep_rdb")
        rows = self.c.fetchall()
        rdb_rows = rows[0][0]
        print("We now have %d lines only_keep_rdb" % (rdb_rows))


        t = time.time()
        q = """
        drop index if exists `idxclid6-4`; -- the other index on reduceDB
        create index `idx_bbb` on `only_keep_rdb` (`id`);
        """
        for l in q.split('\n'):
            self.c.execute(l)
        print("used_later indexes added T: %-3.2f s" % (time.time() - t))

        q = """
        delete from reduceDB
        where reduceDB.rowid not in (select id from only_keep_rdb)
        """
        self.c.execute(q)
        print("Delete from reduceDB T: %-3.2f s" % (time.time() - t))

        t = time.time()
        val = int(options.limit)
        ret = self.c.execute("select count() from reduceDB")
        rows = self.c.fetchall()
        rdb_rows = rows[0][0]
        print("Finally have %d lines of RDB" % (rdb_rows))


    def del_table_and_vacuum(self):
        helper.drop_idxs(self.c)

        t = time.time()
        queries = """
        DROP TABLE IF EXISTS `used_later`;
        DROP TABLE IF EXISTS `only_keep_rdb`;
        DROP TABLE IF EXISTS `used_cl_ids`;
        """
        for q in queries.split("\n"):
            self.c.execute(q)
        print("Deleted tables T: %-3.2f s" % (time.time() - t))

        q = """
        vacuum;
        """

        t = time.time()
        lev = self.conn.isolation_level
        self.conn.isolation_level = None
        self.c.execute(q)
        self.conn.isolation_level = lev
        print("Vacuumed database T: %-3.2f s" % (time.time() - t))


if __name__ == "__main__":
    usage = "usage: %prog [options] sqlitedb"
    parser = optparse.OptionParser(usage=usage)

    parser.add_option("--limit", default=20000, type=int,
                      dest="limit", help="Number of clauses to limit ourselves to")
    parser.add_option("--goalrdb", default=200000, type=int,
                      dest="goal_rdb", help="Number of RDB neeeded")
    parser.add_option("--goalvardata", default=50000, type=int,
                      dest="goal_vardata", help="Number of varData points neeeded")
    parser.add_option("--verbose", "-v", action="store_true", default=False,
                      dest="verbose", help="Print more output")
    parser.add_option("--noidx", action="store_true", default=False,
                      dest="noidx", help="Don't recreate indexes")
    parser.add_option("--fair", "-f", action="store_true", default=False,
                      dest="fair", help="Fair sampling. NOT DEFAULT.")

    (options, args) = parser.parse_args()

    if len(args) < 1:
        print("ERROR: You must give the sqlite file!")
        exit(-1)

    if not options.fair:
        print("NOTE: Sampling will NOT be fair.")
        print("      This is because otherwise, DB will be huge")
        print("      and we need lots of positive datapoints")
        print("      most of which will be from clauses that are more used")


    with QueryDatRem(args[0]) as q:
        q.check_db_sanity()

        q.dangerous()
        if not options.noidx:
            helper.drop_idxs(q.c)
            q.create_indexes1()
        q.recreate_used_ID_table()
        q.remove_too_many_vardata()
        q.fill_used_cl_ids_table()
        q.filter_tables_of_ids()
        q.print_sum_cl_use_distrib()
        q.del_table_and_vacuum()
        print("-------------")
        print("-------------")

        q.dangerous()
        q.create_indexes2()
        q.fill_later_useful_data()
        q.delete_too_many_rdb_rows()

        q.del_table_and_vacuum()

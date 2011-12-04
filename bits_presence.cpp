/***************************************************************************
 *   Copyright (C) 2010, 2011 by Terraneo Federico                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <http://www.gnu.org/licenses/>   *
 ***************************************************************************/

#include <iostream>
#include <fstream>
#include <algorithm>
#include <boost/program_options.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <mysql++.h>
#include "png++/png.hpp"
#include "optimistic.h"

using namespace std;
using namespace boost;
using namespace boost::posix_time;
using namespace boost::date_time;
using namespace boost::program_options;
using namespace mysqlpp;

///Generate statistics for 0=Monday to 5=Saturday, ignore Sunday
const int daysOfWeek=6;

///Lower block starts at this time: 8:00
const posix_time::time_duration startTime(8,0,0);

///How many hours to represent in the image, starting from startTime
const int numBlockPerDay=13;

///How many periods should an hour be split into. Also the # of pixels in a block
const int granularity=24;

const posix_time::time_duration offset=posix_time::time_duration(1,0,0)/granularity;
const int numOffsets=numBlockPerDay*granularity;

struct DatabaseData
{
    string database;
    string connection;
    string user;
    string password;
    string query;
};

/**
 * Generate an image starting from the collected data
 */
class ImageGenerator
{
public:
    /**
     * Generate the image
     * \param data array of chars in range [0..255], 0=red, 255=green
     * \param inputFileName png image to use as template
     * \param outputFileName generated image
     */
    static void generateImage(
        unsigned char data[daysOfWeek][numBlockPerDay][granularity],
        const string& inputFileName, const string& outputFileName)
    {
        png::image<png::rgb_pixel> img(inputFileName);
        for(int d=0;d<daysOfWeek;d++)
            for(int b=0;b<numBlockPerDay;b++) drawBlock(img,data[d][b],d,b);
        img.write(outputFileName);
    }

private:
    /**
     * Fill a single block
     * \param img image object
     * \param block array of colors, [0..255], 0=red, 255=green
     * \param d which day? (x coord)
     * \param b which hour (y coord)
     */
    static void drawBlock(png::image<png::rgb_pixel>& img,
            unsigned char block[granularity], int d, int b)
    {
        for(int y=0;y<granularity;y++)
        {
            int color=block[y];
            int g=min(255,color*2);
            int r=min(255,(255-color)*2);
            png::rgb_pixel pixel(r,g,0);
            for(int x=0;x<xBlock;x++)
                img[yOffset+b*(granularity+1)+y][xOffset+d*(xBlock+1)+x]=pixel;
        }
    }

    static const int xOffset=61;  //x offset of first block
    static const int yOffset=31;  //y offset of first block
    static const int xBlock=50-1; //length of a block
};

/**
 * Reads the bits database and generates a list of time periods, each of which
 * starts and ends in the same day (longer periods are split)
 * \param open periods in which poul is open are returned here
 * \param dbd data required to connect to the databse and perform the query
 * \throws runtime_error if it can't get data from the database
 */
void readLog(vector<time_period>& open, const DatabaseData& dbd)
{
    TCPConnection conn(dbd.connection.c_str(),dbd.database.c_str(),
                    dbd.user.c_str(),dbd.password.c_str());

    Query query=conn.query(dbd.query);
    StoreQueryResult res=query.store();
    if(!res) throw(runtime_error("Database query failed"));
    //if(res.num_colums()!=2) throw(runtime_error("Query result is wrong"));

    int status=1; //We start looking for an open, therefore an 1
    ptime saved;
    for(int i=0;i<res.num_rows();i++)
    {
        string timestamp(res[i][0]);
        string value(res[i][1]);
        //cout<<"<"<<timestamp<<","<<value<<">"<<endl;
        if(value.empty() || status!=(value.at(0)-'0')) continue; //Duplicated
        ptime pt(time_from_string(string(res[i][0])));
        //cout<<pt<<endl;
        if(status==1) saved=pt;
        else {
            //A period should start and end in the same day
            //If it doesn't, normalize it by splitting in more periods.
            while(pt.date()>saved.date())
            {
                open.push_back(time_period(
                        saved,ptime(saved.date(),
                        posix_time::time_duration(23,59,59))));

                saved=ptime(saved.date()+gregorian::date_duration(1),
                        posix_time::time_duration(0,0,0));
            }
            //If needs to normalize, insert last period, otherwise insert the
            //only period
            open.push_back(time_period(saved,pt));
        }
        status=1-status;
    }
}

int main()
{
    //Parse config file
    options_description desc;
    desc.add_options()
        ("database",value<string>())
        ("connection",value<string>())
        ("user",value<string>())
        ("password",value<string>())
        ("query",value<string>())
        ("input_image",value<string>())
        ("output_image",value<string>())
    ;
    variables_map vm;
    ifstream configFile("bits_presence.conf");
    store(parse_config_file(configFile,desc),vm);
    notify(vm);
    if(!vm.count("database") || !vm.count("connection") || !vm.count("user") ||
       !vm.count("password") || !vm.count("query") ||
       !vm.count("input_image") || !vm.count("output_image"))
        throw(runtime_error("Configuration file is missing or incomplete"));
    
    //Delete previous file, so that if the program fails no png image remains
    remove(vm["output_image"].as<string>().c_str());

    //Read log from database
    vector<time_period> open;
    DatabaseData dbd;
    dbd.database=vm["database"].as<string>();
    dbd.connection=vm["connection"].as<string>();
    dbd.user=vm["user"].as<string>();
    dbd.password=vm["password"].as<string>();
    dbd.query=vm["query"].as<string>();
    //cout<<"db="<<dbd.database<<"\ncn="<<dbd.connection<<"\nus="<<dbd.user
    //    <<"\npw="<<dbd.password<<"\nq ="<<dbd.query<<endl;
    readLog(open,dbd);

    //Used to know when to scale a day, avoiding the error of scaling more
    //than one time if more periods refer to the same day
    gregorian::date lastPeriodDay;
    lastPeriodDay=open.at(0).begin().date()-gregorian::date_duration(1);
    unsigned char data[daysOfWeek][numBlockPerDay][granularity];
    memset(data,0,sizeof(data));

    //Compute probabilities
    for(vector<time_period>::iterator it=open.begin();it!=open.end();++it)
    {
        //Should never happen, already taken care of in parseLog()
        if(it->begin().date()!=it->end().date())
            throw(runtime_error("period across midnight"));

        while(it->begin().date()!=lastPeriodDay)
        {
            lastPeriodDay+=gregorian::date_duration(1);
            int dow=lastPeriodDay.day_of_week();
            if(dow==0) continue; //Skip sundays
            dow--;//map to range 0=Monday, 5=Saturday
            //Scale the whole day
            for(int i=0;i<numOffsets;i++)
                data[dow][i/granularity][i%granularity]/=2;
        }

        int dow=it->begin().date().day_of_week();
        if(dow==0) continue; //Skip sundays
        dow--;//map to range 0=Monday, 5=Saturday

        ptime start=ptime(it->begin().date(),startTime);
        bool periodStarted=false;
        for(int i=0;i<numOffsets;i++)
        {
            if(it->contains(start))
            {
                data[dow][i/granularity][i%granularity] |= 0x80;
                periodStarted=true;
            } else if(periodStarted) break;
            start+=offset;
        }
    }

    gregorian::date yesterday;
    yesterday=gregorian::day_clock::local_day()-gregorian::date_duration(1);
    while(lastPeriodDay<yesterday)
    {
        lastPeriodDay+=gregorian::date_duration(1);
        int dow=lastPeriodDay.day_of_week();
        if(dow==0) continue; //Skip sundays
        dow--;//map to range 0=Monday, 5=Saturday
        //Scale the whole day
        for(int i=0;i<numOffsets;i++)
            data[dow][i/granularity][i%granularity]/=2;
    }

    //Scale everything
    for(int i=0;i<daysOfWeek;i++)
       for(int j=0;j<numBlockPerDay;j++)
           for(int k=0;k<granularity;k++)
               data[i][j][k]=optimisticFunction[data[i][j][k]];

    //Generate output image
    ImageGenerator::generateImage(data,vm["input_image"].as<string>(),
        vm["output_image"].as<string>());
}

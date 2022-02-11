import sqlite3
import time
import datetime
import random
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
from dateutil import parser
from matplotlib import style
style.use('fivethirtyeight')

conn = sqlite3.connect('test.db')
c = conn.cursor()

plt.rcParams['lines.linewidth'] = 1
plt.rcParams['axes.grid'] = True 
plt.figure(figsize=(50,4), dpi = 600)



#c.execute("SELECT TIME, ROUND(AVG(data)) FROM Mic_Datas WHERE sock = '78:E3:6D:12:17:A0' GROUP BY TIME")
#c.execute("SELECT TIME, data FROM Mic_Datas WHERE sock = '78:E3:6D:12:17:A0'")
c.execute("SELECT TIME, ROUND(AVG(data)) FROM Mic_Datas WHERE sock = '58:BF:25:99:B4:D8' GROUP BY TIME")


data = c.fetchall()

dates = []
values = []
    
for row in data:
    dates.append(parser.parse(row[0]))
    values.append(row[1])

plt.plot(dates,values,'b', label='58:BF:25:99:B4:D8')

c.execute("SELECT TIME, ROUND(AVG(data)) FROM Mic_Datas WHERE sock = '78:E3:6D:9:37:3C' GROUP BY TIME")


data = c.fetchall()

dates = []
values = []
    
for row in data:
    dates.append(parser.parse(row[0]))
    values.append(row[1])
plt.plot(dates,values,'r', label='78:E3:6D:9:37:3C')
plt.legend(loc='upper right')
plt.savefig('Mic.png')

